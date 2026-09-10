/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "cpu.h"

#include "context.h"
#include "gic2.h"
#include "kernel/memory.h" /* kmalloc/kfree/KERNEL_TYPE */
#include "kernel/string.h" /* kmemcpy/kmemset */

extern boot_info_t* boot_info;
u32 cpus_id[MAX_CPU];

// 31:14-N -> va[29:20] va[19:12]
#define TTBCRN_4K 0b010
#define TTBCRN_16K 0b000
#define TTBCR_LPAE 1 << 31

/* data cache clean by MVA to PoC */
void dccmvac(unsigned long mva) {
  asm volatile("mcr p15, 0, %0, c7, c10, 1" : : "r"(mva) : "memory");
}

int cpu_pmu_version() {
  u32 pmu_id = 0;
  // asm volatile("MRC p15, 0, %0, c9, c0, 0" : "=r"(pmu_id));
  // PMU 版本信息
  return (pmu_id >> 4) & 0xF;
}

/* ================= 内存带宽 / 缓存属性自测 =================
 * 用 PMU 周期计数器（与主频无关，输出 bytes/cycle）测：
 *   1) 4KB / 64KB / 512KB 内核内存拷贝速度；
 *   2) 64KB 读、写分离速度；
 *   3) SCTLR 与目标页的真实页描述符（判断是否真的走 cache）。
 *
 * 判读要点：
 *   - 曲线应"越小越快"（4KB ≥ 64KB ≥ 512KB）。若反过来，说明属性/缓存异常；
 *   - 描述符低 12 位里 bit3(C)/bit2(B) 必须为 1 才是可缓存；
 *     flags=0 时描述符 = L2_DESC = 0x432 = TEX=000,C=0,B=0 = Strongly-ordered；
 *   - 参考（t113@~1GHz）：可缓存时 4KB ≈ 0.22 B/cyc；
 *     Strongly-ordered 时仅 0.004 B/cyc。 */
static inline void pmu_cyc_init(void) {
  u32 v;
  asm volatile("mrc p15,0,%0,c9,c12,0" : "=r"(v)); /* PMCR */
  v |= (1u << 0);                                  /* E  = 使能 */
  v &= ~(1u << 3);                                 /* D  = 0(不分频) */
  v |= (1u << 1);                                  /* P  = 复位事件计数 */
  v |= (1u << 2);                                  /* C  = 复位周期计数 */
  asm volatile("mcr p15,0,%0,c9,c12,0" : : "r"(v));
  asm volatile("mcr p15,0,%0,c9,c12,1" : : "r"(1u << 31)); /* 使能 CCNT */
}

static inline u32 pmu_cyc(void) {
  u32 v;
  asm volatile("mrc p15,0,%0,c9,c13,0" : "=r"(v)); /* PMCCNTR */
  return v;
}

/* 拷贝 size 字节 iters 次；返回 bytes/cycle×100，周期数回填 out_cyc */
static u32 mem_copy_bpc(u32 size, int iters, u32* out_cyc) {
  u8* a = (u8*)kmalloc(size, KERNEL_TYPE);
  u8* b = (u8*)kmalloc(size, KERNEL_TYPE);
  if (a == NULL || b == NULL) {
    return 0;
  }
  kmemset(a, 0x5a, size);
  kmemset(b, 0, size);
  pmu_cyc_init();
  u32 c0 = pmu_cyc();
  for (int i = 0; i < iters; i++) {
    kmemcpy(b, a, size);
  }
  u32 cyc = pmu_cyc() - c0;
  if (cyc == 0) {
    cyc = 1;
  }
  kfree(a);
  kfree(b);
  if (out_cyc != NULL) {
    *out_cyc = cyc;
  }
  return (u32)(((u64)size * (u64)iters * 100u) / (u64)cyc);
}

void cpu_mem_bw_test(void) {
  u32 cyc = 0, bpc;
  kprintf("==== MEMBW test (PMU cycles) ====\n");
  bpc = mem_copy_bpc(4 * 1024, 4000, &cyc);
  kprintf("MEMBW 4KB  : %u.%02u B/cyc  cyc=%u\n", bpc / 100, bpc % 100, cyc);
  bpc = mem_copy_bpc(64 * 1024, 400, &cyc);
  kprintf("MEMBW 64KB : %u.%02u B/cyc  cyc=%u\n", bpc / 100, bpc % 100, cyc);
  bpc = mem_copy_bpc(512 * 1024, 40, &cyc);
  kprintf("MEMBW 512KB: %u.%02u B/cyc  cyc=%u\n", bpc / 100, bpc % 100, cyc);

  /* 读/写分离 + 实机 SCTLR/描述符 */
  {
    u32* q = (u32*)kmalloc(64 * 1024, KERNEL_TYPE);
    if (q == NULL) {
      return;
    }
    volatile u32 sum = 0;
    u32 n = 64 * 1024 / 4;
    for (u32 i = 0; i < n; i++) {
      q[i] = i;
    }
    pmu_cyc_init();
    u32 c0 = pmu_cyc();
    for (int r = 0; r < 100; r++) {
      for (u32 i = 0; i < n; i++) {
        sum += q[i];
      }
    }
    u32 cr = pmu_cyc() - c0;
    c0 = pmu_cyc();
    for (int r = 0; r < 100; r++) {
      for (u32 i = 0; i < n; i++) {
        q[i] = i ^ (u32)r;
      }
    }
    u32 cw = pmu_cyc() - c0;
    kprintf("MEMRW 64KB x100: READ cyc=%u  WRITE cyc=%u  bytes=%u  sum=%u\n", cr, cw,
            6553600u, (u32)sum);

    u32 sctlr, ttbcr, ttbr0 = 0, ttbr1 = 0, l1e = 0, l2e = 0, va = (u32)q;
    asm volatile("mrc p15,0,%0,c1,c0,0" : "=r"(sctlr));
    asm volatile("mrc p15,0,%0,c2,c0,2" : "=r"(ttbcr));
    asm volatile("mrc p15,0,%0,c2,c0,0" : "=r"(ttbr0));
    asm volatile("mrc p15,0,%0,c2,c0,1" : "=r"(ttbr1));
    {
      u32 base = ((ttbcr & 7u) == 0u || va < 0x80000000u) ? ttbr0 : ttbr1;
      u32* l1 = (u32*)(base & 0xFFFFC000u);
      l1e = l1[va >> 20];
      if ((l1e & 3u) == 1u) {
        l2e = ((u32*)(l1e & 0xFFFFFC00u))[(va >> 12) & 0xFFu];
      }
    }
    kprintf("MEMATTR va=%x sctlr=%x M=%u C=%u I=%u ttbcr=%x l1e=%x l2e=%x\n", va, sctlr,
            sctlr & 1u, (sctlr >> 2) & 1u, (sctlr >> 12) & 1u, ttbcr, l1e, l2e);
    kfree(q);
  }
  kprintf("==== MEMBW test end ====\n");
}

void cpu_pmu_enable(int enable, u32 timer) {
  if (enable == 1) {
    // 用户态是否能访问
    //  asm volatile("mcr p15, 0, %0, c9, c14, 0" ::"r"(1));
    // 使能PMU
    asm("MCR p15, 0, %0, c9, c12, 0" ::"r"(enable | 16));
    // 使能计数器0
    asm("MCR p15, 0, %0, c9, c12, 1" ::"r"(timer));

  } else if (enable == 0) {
    asm volatile("mcr p15, 0, %0, c9, c12, 0" ::"r"(0));
    // 清零计数器0
    asm("MCR p15, 0, %0, c9, c12, 2" ::"r"(timer));
    //
    // asm volatile("mcr p15, 0, %0, c9, c14, 0" :: "r"(0));
  }
}

unsigned int cpu_cyclecount(void) {
  unsigned int value;
  // Read CCNT Register
  asm volatile("mrc p15, 0, %0, c9, c13, 0\t\n" : "=r"(value));
  return value;
}

unsigned int cpu_get_cpsr() {
  unsigned int cpsr_value;

  asm volatile("mrs %0, cpsr" : "=r"(cpsr_value));
  return cpsr_value;
}

u32 read_dfar() {
  u32 val = 0;
  asm volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(val));
  return val;
}

u32 cpu_get_fault() { return read_dfar(); }

u32 read_dfsr() {
  u32 val = 0;
  asm volatile("mrc p15, 0, %0, c5, c0, 0" : "=r"(val));
  return val;
}

u32 read_pc() {
  u32 val = 0;
  asm volatile("ldr %0,[r15]" : "=r"(val));
  return val;
}

u32 read_ifsr() {
  u32 val = 0;
  asm volatile("mrc p15, 0, %0, c5, c0, 1" : "=r"(val));
  return val;
}

u32 read_fp() {
  u32 val = 0;
  asm volatile("mov %0,fp" : "=r"(val));
  return val;
}

static inline u32 read_ttbcr(void) {
  u32 val = 0;
  asm volatile("mrc p15, 0, %0, c2, c0, 2" : "=r"(val));
  return val;
}

static inline void write_ttbcr(u32 val) {
  asm volatile("mcr p15, 0, %0, c2, c0, 2" : : "r"(val) : "memory");
}

void write_ttbr0(u32 val) {
  asm volatile("mcr p15, 0, %0, c2, c0, 0" : : "r"(val) : "memory");
}

static inline u32 read_ttbr0() {
  u32 val = 0;
  asm volatile("mrc p15, 0, %0, c2, c0, 0" : "=r"(val));
  return val;
}

static inline void write_ttbr1(u32 val) {
  asm volatile("mcr p15, 0, %0, c2, c0, 1" : : "r"(val) : "memory");
}

static inline u32 read_ttbr1() {
  u32 val = 0;
  asm volatile("mrc p15, 0, %0, c2, c0, 1" : "=r"(val));
  return val;
}

u32 cpu_set_domain(u32 val) {
  u32 old;
  asm volatile("mrc p15, 0, %0, c3, c0,0\n" : "=r"(old));
  asm volatile("mcr p15, 0, %0, c3, c0,0\n" : : "r"(val) : "memory");
  return old;
}

/* invalidate unified TLB by MVA and ASID */
void tlbimva(unsigned long mva) {
  asm volatile("mcr p15, 0, %0, c8, c7, 1" : : "r"(mva) : "memory");
}

void cpu_icache_disable() { asm("mcr  p15, #0, r0, c7, c7, 0\n"); }

void cpu_invalid_tlb() {
  //kprintf("cpu_invalid_tlb cpsr %x\n", 1);
  asm volatile("mcr p15, 0, %0, c8, c7, 0" : : "r"(0));  // unified tlb
  //kprintf("cpu_invalid_tlb1 cpsr %x\n", 2);
  asm volatile("mcr p15, 0, %0, c8, c6, 0" : : "r"(0));  // data tlb
  //kprintf("cpu_invalid_tlb3 cpsr %x\n", 3);
  asm volatile("mcr p15, 0, %0, c8, c5, 0" : : "r"(0));  // instruction tlb
  //kprintf("cpu_invalid_tlb4 cpsr %x\n", 4);

  dsb();
  isb();
}

void cp15_invalidate_icache(void) {
  asm volatile(
      "mov r0, #0\n"
      "mcr p15, 0, r0, c7, c5, 0\n"  // icache all
      "mcr p15, 0, r0, c7, c5, 6\n"  // branch prediction
      "dsb\n"
      :
      :
      : "r0", "memory");
}

void cpu_disable_l1_cache() {
  u32 reg;
  asm("mrc p15, 0, %0, c1, c0, 0" : "=r"(reg) : : "cc");  // SCTLR
  reg |= 1 << 12;  // Instruction cache enable:
  reg |= 1 << 2;   // Cache enable.
  asm volatile("mcr p15, 0, %0, c1, c0, #0" : : "r"(reg) : "cc");  // SCTLR
}

void cpu_set_page(u32 page_table) {
  // Disable MMU
  cpu_disable_page();

  // dccmvac(page_table);

  // set ttbcr0
  write_ttbr0(page_table);
  isb();
  write_ttbr1(page_table);
  isb();
  write_ttbcr(TTBCRN_16K);
 

  dmb();
  dsb();
  isb();

  // set all permission
  // cpu_set_domain(~0);
  // cpu_set_domain(0);


}

void cpu_disable_page() {
  u32 reg;
  // read mmu
  asm("mrc p15, 0, %0, c1, c0, 0" : "=r"(reg) : : "cc");
  reg &= ~0x1;
  asm volatile("mcr p15, 0, %0, c1, c0, #0" : : "r"(reg) : "cc");
}

void cpu_enable_smp_mode() {
  // Enable SMP mode for CPU0
  // asm volatile(
  //   "mrc p15, 1, r0, R1, C15\n" // Read CPUECTLR.
  //   "orr r0, r0, #1 << 6 \n" // Set SMPEN.
  //   "mcr p15, 1, R0, R1, C15"); // Write CPUECTLR.

  asm volatile(
      "mrc p15, 0, r0, c1, c0, 1\n"
      "orr r0, r0, #1 << 6\n"
      "mcr p15, 0, r0, c1, c0, 1\n");
}

static void cpu_enable_ca7_smp(void) {
  u32 val;

  /* Read MIDR */
  asm volatile("mrc p15, 0, %0, c0, c0, 0\n\t" : "=r"(val));
  val = (val >> 4);
  val &= 0xf;

  /* Only set the SMP for Cortex A7 */
  if (val == 0x7) {
    /* Read auxiliary control register */
    asm volatile("mrc p15, 0, %0, c1, c0, 1\n\t" : "=r"(val));

    if (val & (1 << 6)) return;

    /* Enable SMP */
    val |= (1 << 6);

    /* Write auxiliary control register */
    asm volatile("mcr p15, 0, %0, c1, c0, 1\n\t" : : "r"(val));

    dsb();
    isb();
  }
}

inline void cpu_invalidate_tlbs(void) {
  asm("mcr p15, 0, r0, c8, c7, 0\n"
      "mcr p15,0,0,c7,c10,4\n"
      :
      :
      : "r0", "memory");
}

static inline uint32_t get_ccsidr(void) {
  uint32_t ccsidr;

  __asm__ __volatile__("mrc p15, 1, %0, c0, c0, 0" : "=r"(ccsidr));
  return ccsidr;
}

static inline void __v7_cache_flush_range(uint32_t start, uint32_t stop,
                                          uint32_t line) {
  uint32_t mva;

  start &= ~(line - 1);
  if (stop & (line - 1)) stop = (stop + line) & ~(line - 1);
  for (mva = start; mva < stop; mva = mva + line) {
    __asm__ __volatile__("mcr p15, 0, %0, c7, c14, 1" : : "r"(mva));
  }
}

/*
 * Flush range(clean & invalidate), affects the range [start, stop - 1]
 */
void cpu_cache_flush_range(unsigned long start, unsigned long stop) {
  uint32_t ccsidr;
  uint32_t line;

  ccsidr = get_ccsidr();
  line = ((ccsidr & 0x7) >> 0) + 2;
  line += 2;
  line = 1 << line;
  __v7_cache_flush_range(start, stop, line);
  dsb();
}

static inline void __v7_cache_inv_range(uint32_t start, uint32_t stop,
                                        uint32_t line) {
  uint32_t mva;

  start &= ~(line - 1);
  if (stop & (line - 1)) stop = (stop + line) & ~(line - 1);
  for (mva = start; mva < stop; mva = mva + line) {
    __asm__ __volatile__("mcr p15, 0, %0, c7, c6, 1" : : "r"(mva));
  }
}
/*
 * Invalidate range, affects the range [start, stop - 1]
 */
void cache_inv_range(unsigned long start, unsigned long stop) {
  uint32_t ccsidr;
  uint32_t line;

  ccsidr = get_ccsidr();
  line = ((ccsidr & 0x7) >> 0) + 2;
  line += 2;
  line = 1 << line;
  __v7_cache_inv_range(start, stop, line);
  dsb();
}

void cpu_enable_page() {
  cpu_enable_smp_mode();
  // cache_inv_range(0, ~0);

  u32 reg;
  // read mmu
  asm("mrc p15, 0, %0, c1, c0, 0" : "=r"(reg) : : "cc");  // SCTLR
  reg |= 0x1;                                             // M enable mmu
  /* 勿开 AFE/TEX remap：mm.h 用的是传统 TEX/C/B + AP 编码。
   * TRE=1 却未配 PRRR/NMRR 时，实机（T113）上用户堆可能变成 Device/
   * 错误属性，musl mallocng 元数据错乱 → a_crash；QEMU 往往仍能跑。 */
  reg &= ~(1u << 29);  // AFE off
  reg &= ~(1u << 28);  // TEX remap off
  reg |= 1 << 12;      // Instruction cache enable
  reg |= 1 << 2;       // Data cache enable
  reg &= ~(1 << 1);    // Alignment check disable
  reg |= 1 << 11;      // Branch prediction enable
  asm volatile("mcr p15, 0, %0, c1, c0, #0" : : "r"(reg) : "cc");  // SCTLR

  // Invalidate L1 Caches Invalidate Instruction cache
  cp15_invalidate_icache();

  // Invalidate Data cache
  cache_inv_range(0, ~0);

  cpu_invalid_tlb();

  dmb();
  dsb();
  isb();
}

extern void lcpu_wait_start(int cpu);

void cpu_init(int cpu) {
  if (cpu != 0) {
    lcpu_wait_start(cpu);
  }
  // cpu_enable_smp_mode();
  // cpu_enable_ca7_smp();
  for (int i = 0; i < MAX_CPU; i++) {
    cpus_id[i] = i;
  }
}

void cpu_halt() {
  for (;;) {
    asm("wfi");
  };
}

void cpu_wait() { asm("wfi"); }

ulong cpu_get_cs(void) {
  ulong result;

  return result;
}

int cpu_tas(volatile int* addr, int newval) {
  int result = newval;
  result = __sync_val_compare_and_swap(addr, newval, result);
  return result;
}

void cpu_backtrace(void* tfp, void** buf, int size) {
  int topfp = tfp;  // read_fp();
  for (int i = 0; i < size; i++) {
    u32 fp = *(((u32*)topfp) - 3);
    u32 sp = *(((u32*)topfp) - 2);
    u32 lr = *(((u32*)topfp) - 1);
    u32 pc = *(((u32*)topfp) - 0);
    if (i == 0) {
      *buf++ = pc;
    }  // top frame
    if (fp != 0) {
      *buf++ = lr;
      // kprintf(" %x\n", lr);
    }  // middle frame
    else {
      kprintf("bottom frame %x\n", pc);
    }  // bottom frame, lr invalid
    if (fp == 0) break;
    topfp = fp;
  }
}

int cpu_get_number() { return boot_info->tss_number; }

u32 cpu_get_id() {
  int cpu = 0;
  /* 必须【无条件】读 MPIDR：本文件只用于 armv7-a，MPIDR 一定存在（单核 SoC 也
   * 返回合法值，&0xf 即为 0）。绝不能把它挂在 MP_ENABLE 下 —— 引导代码
   * boot/arm/boot-armv7-a.s 的 _start 会按 MPIDR 把【任何非 0 核】送进
   * apu_entry → start_apu_kernel() 进内核；此时若这里恒返回 0，那个核就会
   * "以为自己是 CPU0"：共用 current_threads[0]、同一份 ctx->ksp 与内核栈，
   * 甚至把 kernel_init() 的 cpu==0 分支再跑一遍（实测 t113-s3：
   * platform.h 里 MP_ENABLE 被注释掉，从核重复初始化整个内核，把 shell 线程踩死）。 */
  __asm__ volatile("mrc p15, #0, %0, c0, c0, #5\n" : "=r"(cpu));
  return cpu & 0xf;
}

u32 cpu_get_index(int idx) {
  if (idx < 0 || idx > cpu_get_number()) {
    kprintf("out of bound get cpu idx\n");
    return 0;
  }
  return cpus_id[idx];
}

int cpu_init_id(u32 id) {
  // kprintf("cpu init id %d\n", id);
  ipi_enable(id);
  return 0;
}

int cpu_start_id(u32 id, u32 entry) {
  // start at  at entry-point on boot init.c
  // kprintf("cpu start id %d entry: %x\n", id,entry);
  lcpu_send_start(id, entry);
  return 0;
}

u32 read_cntv_tval(void) {
  u32 val;
  asm volatile("mrc p15, 0, %0, c14, c3, 0" : "=r"(val));
  return val;
}

void write_cntv_tval(u32 val) {
  asm volatile("mcr p15, 0, %0, c14, c3, 0" ::"r"(val));
  return;
}

u32 read_cntfrq(void) {
  u32 val;
  asm volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(val));
  return val;
}

void enable_cntv(u32 cntv_ctl) {
  asm volatile("mcr p15, 0, %0, c14, c3, 1" ::"r"(cntv_ctl));  // write CNTV_CTL
}

void disable_cntv(u32 cntv_ctl) {
  asm volatile("mcr p15, 0, %0, c14, c3, 1" ::"r"(cntv_ctl));  // write CNTV_CTL
}

uint64_t read_cntvct(void) {
  uint64_t val;
  asm volatile("mrrc p15, 1, %Q0, %R0, c14" : "=r"(val));
  return (val);
}

/*do fast 64bit div constant
 *	because 52 bit timer can provide 23 years cycle loop
 * 	we can do 64bit divided as below：
 * 	x / 6 = x / (4096/682) = x * 682 / 4096 = (x * 682) >> 12
 *   it will save hundreds of cpu cycle
 */
static __inline uint64_t fast_div64_6(uint64_t x) { return (x * 682) >> 12; }

static inline uint64_t timer_read_sys_usec(void) {  // read microsec
  return fast_div64_6(read_cntvct());
}

uint64_t cpu_read_ms(){
  return read_cntvct()/24000;
}


void cpu_delay_usec(uint64_t count) {
  uint64_t s = timer_read_sys_usec();
  uint64_t t = s + count;
  while (s < t) {
    s = timer_read_sys_usec();
  }
}

void cpu_delay_msec(uint32_t count) { cpu_delay_usec(count * 1000); }

void cpu_delay(int n) {
  // cpu_delay_msec(n);
  while (n > 0) {
    n--;
  }
}