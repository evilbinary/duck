/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "cpu.h"
#include "context.h"
#include "libs/include/kernel/common.h"
#include "libs/include/types.h"

extern boot_info_t* boot_info;
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

// Enable SMP mode (for cache coherency)
void cpu_enable_smp_mode(void) {
  // Enable SMP mode for cache coherency
  // On ARM64, this is done via ACTLR_EL3 or similar
  // For simplicity, we assume firmware has set this up
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
}

// Get CPU number from boot info
int cpu_get_number(void) { 
  return boot_info->tss_number; 
}

// Get current CPU ID
u32 cpu_get_id(void) {
  u64 mpidr;
  asm volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
  return mpidr & 0xF;
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
void cpu_init(void) {
  // Initialize CPU state
  // Clear any pending interrupts, etc.
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
  // Initialize IPI for this CPU
  // For Raspberry Pi 3, use mailbox interrupts
  return 0;
}

int cpu_start_id(u32 id, u32 entry) {
  // Start secondary CPU via mailbox
  // For Raspberry Pi 3, write to mailbox
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
