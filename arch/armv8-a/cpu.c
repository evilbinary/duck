/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "cpu.h"
#include "context.h"
#include "gpio.h"
#include "libs/include/kernel/common.h"
#include "libs/include/kernel/io.h" /* io_print_lock_set_atomic：打印锁原子开关 */
#include "libs/include/types.h"

extern boot_info_t* boot_info;
extern void lcpu_wait_start(int cpu);
extern void lcpu_send_start(u32 cpu, u64 entry);
u64 cpus_id[MAX_CPU];

// Page table control
#define TCR_T0SZ(x)   ((64 - (x)) & 0x3F)
#define TCR_IRGN0(x)  ((x) << 8)
#define TCR_ORGN0(x)  ((x) << 10)
#define TCR_SH0(x)    ((x) << 12)
#define TCR_TG0_4K    (0 << 14)
#define TCR_TG0_16K   (1 << 14)
#define TCR_TG0_64K   (2 << 14)
#define TCR_IPS(x)    ((x) << 32)

// Write page table registers
static inline void write_ttbr0(u64 val) {
  asm volatile("msr ttbr0_el1, %0" : : "r"(val) : "memory");
  isb();
}

static inline void write_ttbr1(u64 val) {
  asm volatile("msr ttbr1_el1, %0" : : "r"(val) : "memory");
  isb();
}

static inline void write_tcr(u64 val) {
  asm volatile("msr tcr_el1, %0" : : "r"(val) : "memory");
  isb();
}

static inline u64 read_tcr(void) {
  u64 val;
  asm volatile("mrs %0, tcr_el1" : "=r"(val));
  return val;
}

// Invalidate instruction cache
void cp15_invalidate_icache(void) {
  asm volatile(
      "ic iallu\n"
      "dsb ish\n"
      "isb\n"
  );
}

/* ---- 统一 cache 接口（armv8-a 强实现） ----------------------------------- */
void cpu_invalidate_icache(void) { cp15_invalidate_icache(); }

void cpu_flush_dcache_range(unsigned long start, unsigned long stop) {
  start &= ~63UL;
  for (unsigned long va = start; va < stop; va += 64) {
    asm volatile("dc civac, %0" : : "r"(va) : "memory");
  }
  asm volatile("dsb ish" ::: "memory");
}

void cpu_invalidate_dcache_range(unsigned long start, unsigned long stop) {
  start &= ~63UL;
  for (unsigned long va = start; va < stop; va += 64) {
    asm volatile("dc ivac, %0" : : "r"(va) : "memory");
  }
  asm volatile("dsb ish" ::: "memory");
}

// Invalidate TLB
void cpu_invalid_tlb(void) {
  asm volatile(
      "tlbi vmalle1is\n"
      "dsb ish\n"
      "isb\n"
  );
}

// Read fault address register (DFAR equivalent)
u64 read_far_el1(void) {
  u64 val;
  asm volatile("mrs %0, far_el1" : "=r"(val));
  return val;
}

u64 cpu_get_fault(void) { 
  u64 val=0;
  val=read_far_el1(); 
  return val;
}

u64 cpu_read_ttbr0(void) {
  u64 val;
  asm volatile("mrs %0, ttbr0_el1" : "=r"(val));
  return val;
}

// Read ESR (exception syndrome)
u64 read_esr_el1(void) {
  u64 val;
  asm volatile("mrs %0, esr_el1" : "=r"(val));
  return val;
}

// Set page table
void cpu_set_page(u64 page_table) {
  write_ttbr0(page_table);
  cpu_invalid_tlb();
  cp15_invalidate_icache();
  dmb();
  isb();
}

/* Pi 5 在 EL1 读/写 CPUECTLR（S3_0_C15_C1_4）会卡死在 page enable。
 * 一致性位由固件在进内核前设置，这里不要再碰。 */
void cpu_enable_smp_mode(void) {}

/* 【分页(MMU)是否已开】覆盖 arch.c 里的弱符号（接口见 arch/cpu.h）。
 * SCTLR_EL1.M（bit0）就是 MMU 使能位。
 * 【为什么必须有】arch.c 的弱默认返回 1 ⇒ arch_init 会把打印锁设成"用独占
 * 指令(ldaxr/stxr)"；而树莓派 boot 阶段把 MMU 关着进内核（page_init 才重开），
 * 此时全部内存都是 Device-nGnRnE，AArch64 对 Device 内存做独占访问监视器不置位、
 * 在 Pi5(A76) 上直接卡死 —— 实测现象：内核第一句 kprintf（interrupt_init 里）
 * 打不出来、[i1b] 之后全无输出（display_init 的直写 puts 不过锁所以正常）。
 * armv7-a 有同款实现（Cortex-A7 上是 data abort）。 */
int cpu_page_enabled(void) {
  u64 sctlr;
  asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
  return (int)(sctlr & 0x1);
}

// Enable MMU and caches
void cpu_enable_page(void) {
  cpu_enable_smp_mode();
  
  u64 sctlr;
  asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
  
  // Enable MMU (M bit)
  sctlr |= (1 << 0);
  // Enable data cache (C bit)
  sctlr |= (1 << 2);
  // Enable instruction cache (I bit)
  sctlr |= (1 << 12);
  
  asm volatile("msr sctlr_el1, %0" : : "r"(sctlr) : "memory");
  isb();
  dsb();

  /* MMU/缓存已开 ⇒ 独占指令可用，把打印锁切回原子实现
   * （与 armv7-a 的 cpu_enable_page 行为一致）。 */
  io_print_lock_set_atomic(1);
}

// Get CPU number from boot info
int cpu_get_number(void) { 
  return boot_info->tss_number; 
}

// Get current CPU ID.
// Pi 3: core id is Aff0. Pi 5 (BCM2712): Aff0 is 0 on every core and the
// core id is Aff1 (0x000 / 0x100 / 0x200 / 0x300). Masking only Aff0 makes
// every Pi 5 core report 0 and re-enter kmain.
u32 cpu_get_id(void) {
  u64 mpidr;
  asm volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
  u32 aff0 = (u32)(mpidr & 0xff);
  if (aff0 != 0) {
    return aff0 & 0xf;
  }
  return (u32)((mpidr >> 8) & 0xff);
}

// Get CPU index
u64 cpu_get_index(int idx) {
  if (idx < 0 || idx >= cpu_get_number()) {
    kprintf("out of bound get cpu idx\n");
    return 0;
  }
  return cpus_id[idx];
}

// Initialize CPU
void cpu_init(int cpu_id) {
  if (cpu_id != 0) {
    lcpu_wait_start(cpu_id);
  }
  for (int i = 0; i < MAX_CPU; i++) {
    cpus_id[i] = i;
  }
}

// Halt CPU
void cpu_halt(void) {
  for (;;) {
    asm volatile("wfi");
  }
}

// Wait for interrupt
void cpu_wait(void) {
  asm volatile("wfi");
}

// Get CS (code segment) - not really applicable on ARM64
u64 cpu_get_cs(void) {
  return 0;
}

// Test and set (atomic)
int cpu_tas(volatile int* addr, int newval) {
  int oldval;
  int result;
  // Use LDAXR/STLXR for atomic operation
  asm volatile(
      "1: ldaxr %w0, [%2]\n"
      "stlxr %w1, %w3, [%2]\n"
      "cbnz %w1, 1b\n"
      : "=&r"(oldval), "=&r"(result)
      : "r"(addr), "r"(newval)
      : "memory"
  );
  return oldval;
}

// Backtrace (simplified)
void cpu_backtrace(void* fp, u64* buf, int max) {
  u64* frame = (u64*)fp;
  int i = 0;
  
  while (frame != NULL && i < max) {
    buf[i++] = frame[1];  // LR is at frame+1
    frame = (u64*)frame[0];  // Previous FP is at frame
  }
}

// PMU functions for ARM64
int cpu_pmu_version(void) {
  u64 pmcr;
  asm volatile("mrs %0, pmcr_el0" : "=r"(pmcr));
  return (pmcr >> 11) & 0xF;  // PMU version bits
}

void cpu_pmu_enable(int enable, u32 counter) {
  if (enable) {
    // Enable PMU
    asm volatile("msr pmcr_el0, %0" : : "r"(0x1 | (1 << 1) | (1 << 2)) : "memory");
    // Enable counter
    u64 cntenset;
    asm volatile("mrs %0, pmcntenset_el0" : "=r"(cntenset));
    cntenset |= (1ULL << counter);
    asm volatile("msr pmcntenset_el0, %0" : : "r"(cntenset) : "memory");
    // Enable user access
    asm volatile("msr pmuserenr_el0, %0" : : "r"(1) : "memory");
  } else {
    u64 cntenclr = (1ULL << counter);
    asm volatile("msr pmcntenclr_el0, %0" : : "r"(cntenclr) : "memory");
  }
}

unsigned int cpu_cyclecount(void) {
  u64 count;
  asm volatile("mrs %0, pmccntr_el0" : "=r"(count));
  return (unsigned int)count;
}

// Multi-processor functions
int cpu_init_id(u32 id) {
  ipi_enable(id);
  return 0;
}

int cpu_start_id(u32 id, u32 entry) {
  lcpu_send_start(id, entry);
  return 0;
}

void cpu_delay(int n) {
  while (n > 0) {
    for (volatile int i = 0; i < 10000; i++);
    n--;
  }
}

void cpu_delay_usec(uint64_t count) {
  u64 freq = read_cntfrq();
  u64 cycles = (freq * count) / 1000000ULL;
  u64 start = read_cntvct();
  while ((read_cntvct() - start) < cycles);
}

void cpu_delay_msec(uint32_t count) {
  cpu_delay_usec(count * 1000ULL);
}

uint64_t cpu_read_ms(void) {
  u64 freq = read_cntfrq();
  return read_cntvct() / (freq / 1000ULL);
}
