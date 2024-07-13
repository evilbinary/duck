/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "cpu.h"

#include "context.h"

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
  asm volatile("MRC p15, 0, %0, c9, c0, 0" : "=r"(pmu_id));
  // PMU 版本信息
  return (pmu_id >> 4) & 0xF;
}

void cpu_pmu_enable(int enable, u32 timer) {
  if (enable == 1) {
  } else if (enable == 0) {
  }
}

unsigned int cpu_cyclecount(void) {
  unsigned int value = 0;

  return value;
}

void cpu_icache_disable() { asm("mcr  p15, #0, r0, c7, c7, 0\n"); }

void cpu_invalid_tlb() {
  asm volatile("mcr p15, 0, %0, c8, c7, 0" : : "r"(0));  // unified tlb
  asm volatile("mcr p15, 0, %0, c8, c6, 0" : : "r"(0));  // data tlb
  asm volatile("mcr p15, 0, %0, c8, c5, 0" : : "r"(0));  // instruction tlb

  isb();
  dsb();
}

void cp15_invalidate_icache(void) {
  asm volatile(
      "mov r0, #0\n"
      "mcr p15, 0, r0, c7, c7, 0\n"  // Invalidate ICache and DCache
      :
      :
      : "r0", "memory");
  dsb();
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

void cpu_disable_l1_cache() {
  u32 reg;
  asm("mrc p15, 0, %0, c1, c0, 0" : "=r"(reg) : : "cc");  // SCTLR
  reg |= 1 << 12;  // Instruction cache enable:
  reg |= 1 << 2;   // Cache enable.
  asm volatile("mcr p15, 0, %0, c1, c0, #0" : : "r"(reg) : "cc");  // SCTLR
}

void cpu_set_page(u32 page_table) {
  kprintf("cpu_set_page\n");

  // dccmvac(page_table);
  // set ttbcr0
  write_ttbr0(page_table);
  isb();
  kprintf("cpu_set_page61\n");

  // write_ttbr1(page_table);

  kprintf("cpu_set_page62\n");
  isb();
  // write_ttbcr(TTBCRN_16K);

  kprintf("cpu_set_page7\n");
}

void cpu_disable_page() {
  // u32 reg;
  // asm("mcr     p15, #0, r0, c8, c7, #0\n");   // @ invalidate tlb
  // asm("mrc p15, 0, %0, c1, c0, 0" : "=r"(reg) : : "cc");
  // reg &= ~(1 << 0);
  // asm volatile("mcr p15, 0, %0, c1, c0, #0" : : "r"(reg) : "cc");
}

void cpu_enable_smp_mode() {
  // Enable SMP mode for CPU0
  // asm volatile(
  //   "mrc p15, 1, r0, R1, C15\n" // Read CPUECTLR.
  //   "orr r0, r0, #1 << 6 \n" // Set SMPEN.
  //   "mcr p15, 1, R0, R1, C15"); // Write CPUECTLR.

  // asm volatile(
  //     "mrc p15, 0, r0, c1, c0, 1\n"
  //     "orr r0, r0, #1 << 6\n"
  //     "mcr p15, 0, r0, c1, c0, 1\n");
}


inline void cpu_invalidate_tlbs(void) {
  asm("mcr p15, 0, r0, c8, c7, 0\n"
      "mcr p15,0,0,c7,c10,4\n"
      :
      :
      : "r0", "memory");
}

static inline u32 get_ccsidr(void) {
  u32 ccsidr;

  __asm__ __volatile__("mrc p15, 1, %0, c0, c0, 0" : "=r"(ccsidr));
  return ccsidr;
}

static inline u32 get_cache(void) {
  u32 cache;

  __asm__ __volatile__("mrc p15, 0, %0, c0, c0, 1" : "=r"(cache));
  return cache;
}

static inline void __v5_cache_inv_range(u32 start, u32 stop, u32 line) {
  u32 mva;

  start &= ~(line - 1);
  if (stop & (line - 1)) stop = (stop + line) & ~(line - 1);
  for (mva = start; mva < stop; mva = mva + line) {
    __asm__ __volatile__("mcr p15, 0, %0, c7, c6, 1" : : "r"(mva));
  }
}

static inline void __v5_cache_flush_range(u32 start, u32 stop, u32 line) {
  u32 mva;

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
  u32 cache;
  u32 line;

  cache = get_cache();
  line = 1 << ((cache & 0x3) + 3);
  __v5_cache_flush_range(start, stop, line);
  dsb();
}

/*
 * Invalidate range, affects the range [start, stop - 1]
 */
void cache_inv_range(unsigned long start, unsigned long stop) {
  u32 cache;
  u32 line;

  cache = get_cache();
  line = 1 << ((cache & 0x3) + 3);
  __v5_cache_inv_range(start, stop, line);
  dsb();
}

void cache_flush_range(unsigned long start, unsigned long stop) {
  u32 cache;
  u32 line;

  cache = get_cache();
  line = 1 << ((cache & 0x3) + 3);
  __v5_cache_flush_range(start, stop, line);
  dsb();
}

void cpu_enable_page() {
  kprintf("cpu_enable_page\n");

  cpu_enable_smp_mode();
  kprintf("cpu_enable_page1\n");

  u32 reg;
  // read mmu
  asm("mrc p15, 0, %0, c1, c0, 0" : "=r"(reg) : : "cc");  // SCTLR
  reg |= 0x1;                                             // M enable mmu
  // reg |= 1 << 1;  // Alignment check enable.
  reg |= 1 << 2;  // Data Cache enable.
  reg |=  1<<3; //enable write buffer;

  reg |= 1 << 8;  // System protection bit.
  reg |= 1 << 9;  // ROM protection bit.
  // reg|= 1<<23; //0 = VMSAv4/v5 and VMSAv6, subpages enabled 1 = VMSAv6,
  // subpages disabled.

  reg |= 1 << 12;  // Instruction cache enable:

  // reg |= 1 << 13;  // vic low 0  vic hight 1
  asm volatile("mcr p15, 0, %0, c1, c0, #0" : : "r"(reg) : "cc");  // SCTLR

  kprintf("cpu_enable_page2\n");

  // Disable L1 Cache
  cpu_disable_l1_cache();

  kprintf("cpu_enable_page3\n");

  // Invalidate L1 Caches Invalidate Instruction cache
  cp15_invalidate_icache();

  // Invalidate Data cache
  // __builtin___clear_cache(0, ~0);
  cache_inv_range(0, ~0);

  kprintf("cpu_enable_page4\n");

  cpu_invalid_tlb();

  kprintf("cpu_enable_page3\n");
}

void cpu_init(int cpu) {
  // cpu_enable_smp_mode();
  // cpu_enable_ca7_smp();
  for (int i = 0; i < MAX_CPU; i++) {
    cpus_id[i] = i;
  }
}

void cpu_halt() {
  for (;;) {
  };
}

void cpu_wait() {}

ulong cpu_get_cs(void) {
  ulong result;

  return result;
}

int cpu_tas(volatile int* addr, int newval) {
  int result = newval;
  // result = __sync_val_compare_and_swap(addr, newval, result);
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

void cpu_delay_usec(uint64_t count) {
  // uint64_t s = timer_read_sys_usec();
  // uint64_t t = s + count;
  // while (s < t) {
  //   s = timer_read_sys_usec();
  // }
}

void cpu_delay_msec(u32 count) { cpu_delay_usec(count * 1000); }

void cpu_delay(int n) {
  // cpu_delay_msec(n);
  while (n > 0) {
    n--;
  }
}

void cpu_cli() {
  u32 val;
  asm("mrs %[v], cpsr" : [v] "=r"(val)::);
  val |= 0x80;
  asm("msr cpsr_cxsf, %[v]" : : [v] "r"(val) :);
}

void cpu_sti() {
  u32 val;
  asm("mrs %[v], cpsr" : [v] "=r"(val)::);
  val &= ~0x80;
  asm("msr cpsr_cxsf, %[v]" : : [v] "r"(val) :);
}

void cpu_cmpxchg(void* ptr, u32 old_value, u32 new_value) {
  // asm(".word 0xf57ff05f\n" /* dmb sy                */
  //     ".word 0xe1923f9f\n" /* ldrex r3, [r2]        */
  //     ".word 0xe0530000\n" /* subs r0, r3, r0       */
  //     ".word 0x01820f91\n" /* strexeq r0, r1, [r2]  */
  //     ".word 0xf57ff05f\n" /* dmb sy                */
  //     ".word 0xe12fff1e\n" /* bx lr                 */
  // );
}