/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "context.h"
#include "cpu.h"

extern boot_info_t* boot_info;
u32 cpus_id[MAX_CPU];

#define TTBCRN_4K 0b010
#define TTBCRN_16K 0b000
#define TTBCR_LPAE 1 << 31

static inline void write_ttbcr(u32 val) {
  asm volatile("mcr p15, 0, %0, c2, c0, 2" : : "r"(val) : "memory");
}

static inline void write_ttbr0(u32 val) {
  asm volatile("mcr p15, 0, %0, c2, c0, 0" : : "r"(val) : "memory");
}

static inline void write_ttbr1(u32 val) {
  asm volatile("mcr p15, 0, %0, c2, c0, 1" : : "r"(val) : "memory");
}

void cp15_invalidate_icache(void) {
  asm volatile(
      "mov r0, #0\n"
      "mcr p15, 0, r0, c7, c5, 0\n"
      "dsb\n"
      :
      :
      : "r0", "memory");
}

void cpu_invalid_tlb() {
  asm volatile("mcr p15, 0, %0, c8, c7, 0" : : "r"(0));
  asm volatile("mcr p15, 0, %0, c8, c6, 0" : : "r"(0));
  asm volatile("mcr p15, 0, %0, c8, c5, 0" : : "r"(0));
  asm volatile(
      "isb\n"
      "dsb\n");
}

u32 cpu_set_domain(u32 val) {
  u32 old;
  asm volatile("mrc p15, 0, %0, c3, c0,0\n" : "=r"(old));
  asm volatile("mcr p15, 0, %0, c3, c0,0\n" : : "r"(val) : "memory");
  return old;
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

void cpu_set_page(u32 page_table) {
  // cpu_invalid_tlb();
  // cp15_invalidate_icache();

  // dccmvac(page_table);
  // set ttbcr0
  write_ttbr0(page_table);
  // isb();
  write_ttbr1(page_table);
  // isb();
  write_ttbcr(TTBCRN_16K);
  cpu_invalid_tlb();
  dmb();
  isb();
  dsb();
  // set all permission
  // cpu_set_domain(~0);
  // cpu_set_domain(0);
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


void cpu_enable_page() {
  cpu_enable_smp_mode();
  cache_inv_range(0, ~0);
  // mmu_inv_tlb();

  u32 reg;
  // read mmu
  asm("mrc p15, 0, %0, c1, c0, 0" : "=r"(reg) : : "cc");  // SCTLR
  reg |= 0x1;                                             // M enable mmu
  // reg|=(1<<29);//AFE
  // reg |= 1 << 28; //TEX remap enable.
  reg |= 1 << 12;  // Instruction cache enable:
  reg |= 1 << 2;   // Cache enable.
  // reg |= 1 << 1;   // Alignment check enable.
  reg |= 1 << 11;  // Branch prediction enable
  asm volatile("mcr p15, 0, %0, c1, c0, #0" : : "r"(reg) : "cc");  // SCTLR
  dsb();
  isb();
}


static inline uint32_t get_ccsidr(void) {
  uint32_t ccsidr;

  __asm__ __volatile__("mrc p15, 1, %0, c0, c0, 0" : "=r"(ccsidr));
  return ccsidr;
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

int cpu_get_number() { return boot_info->tss_number; }

u32 cpu_get_id() {
  int cpu = 0;
#if MP_ENABLE
  __asm__ volatile("mrc p15, #0, %0, c0, c0, #5\n" : "=r"(cpu));
#endif
  return cpu & 0xf;
}

u32 cpu_get_index(int idx) {
  if (idx < 0 || idx > cpu_get_number()) {
    kprintf("out of bound get cpu idx\n");
    return 0;
  }
  return cpus_id[idx];
}



void cpu_init() {


}

void cpu_halt() {
  for (;;) {
    
  };
}


void cpu_wait(){

}

ulong cpu_get_cs(void) {
  ulong result;

  return result;
}

int cpu_tas(volatile int* addr, int newval) {
  int result = newval;

  return result;
}

void cpu_backtrace(void) {
  
}
