/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "cpu.h"

#include "context.h"

extern boot_info_t* boot_info;
u32 cpus_id[MAX_CPU];

void cpu_init() {}

void cpu_halt() {
  for (;;) {
    cpu_wait();
  };
}

void cpu_wait() { asm("wfi"); }

ulong cpu_get_cs(void) {
  ulong result;

  return result;
}

int cpu_tas(volatile int* addr, int newval) {
  int result = newval;

  return result;
}

int cpu_pmu_version() {
  u32 pmu_id = 0;
  // PMU 版本信息
  return (pmu_id >> 4) & 0xF;
}

void cpu_pmu_enable(int enable, u32 timer) {

}

unsigned int cpu_cyclecount(void) {
  unsigned int value;

  return value;
}

int cpu_get_number() { return boot_info->tss_number; }

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


void cpu_delay(int n) {
  // cpu_delay_msec(n);
  while (n > 0) {
    n--;
  }
}

void cpu_backtrace(void) {}

void cpu_set_page(u32 page_table) {
  // Sv32 方式 10+10+12 PPN =22  mode 1
  u32 page = page_table >> 12 | (1 << 30);

  // Set the value of satp register
  asm volatile("csrw satp, %0" : : "r"(page));
  // Flush the TLB (Translation Lookaside Buffer)
  asm volatile("sfence.vma");
}

void cpu_enable_page() {
  // 使用 MMU 映射虚拟页面到物理页面
  asm volatile("sfence.vma");
  // 启用 MMU
  asm volatile("li t0, 0x80000000");
  asm volatile("csrs sstatus, t0");
}

u32 cpu_get_id() {
  int cpu = 0;
#if MP_ENABLE
  asm volatile("csrr %[cpu], mhartid" : [cpu] "=r"(cpu) : :);
#endif
  return cpu & 0xf;
}

u32 cpu_get_fault() {
  u32 fault;
  // 从cpuid CSR中读取最后一位（bit 31），即mstatus寄存器中MIE的值。
  asm volatile("csrr %0, mcause" : "=r"(fault));
  // 返回值
  return fault;
}

u32 cpu_read_mie() {
  u32 x;
  asm volatile("csrr %0, mie" : "=r"(x));
  return x;
}

void cpu_write_mie(u32 x) { asm volatile("csrw mie, %0" : : "r"(x)); }

u32 cpu_read_mstatus() {
  u32 x;
  asm volatile("csrr %0, mstatus" : "=r"(x));
  return x;
}

void cpu_write_mstatus(u32 x) { asm volatile("csrw mstatus, %0" : : "r"(x)); }

void cpu_write_mtvec(u32 x) { asm volatile("csrw mtvec, %0" : : "r"(x)); }

u32 cpu_read_sie() {
  u32 x;
  asm volatile("csrr %0, sie" : "=r"(x));
  return x;
}

void cpu_write_sie(u32 x) { asm volatile("csrw sie, %0" : : "r"(x)); }

u32 cpu_read_sstatus() {
  u32 x;
  asm volatile("csrr %0, sstatus" : "=r"(x));
  return x;
}

void cpu_write_sstatus(u32 x) { asm volatile("csrw sstatus, %0" : : "r"(x)); }

void cpu_write_stvec(u32 x) { asm volatile("csrw stvec, %0" : : "r"(x)); }

void cpu_write_stimecmp(u32 x) { asm volatile("csrw 0x14D, %0\n" : "=r"(x)); }

u32 cpu_read_medeleg() {
  u32 x;
  asm volatile("csrr %0, medeleg" : "=r"(x));
  return x;
}

void cpu_write_medeleg(u32 x) { asm volatile("csrw medeleg, %0" : : "r"(x)); }

u32 cpu_read_mideleg() {
  u32 x;
  asm volatile("csrr %0, mideleg" : "=r"(x));
  return x;
}

void cpu_write_mideleg(u32 x) { asm volatile("csrw mideleg, %0" : : "r"(x)); }

u32 cpu_read_scause() {
  u32 x;
  asm volatile("csrr %0, scause" : "=r"(x));
  return x;
}

void cpu_write_sip(u32 x) { asm volatile("csrw sip, %0" : : "r"(x)); }

u32 cpu_read_sip() {
  u32 x;
  asm volatile("csrr %0, sip" : "=r"(x));
  return x;
}

void cpu_write_mip(u32 x) { asm volatile("csrw mip, %0" : : "r"(x)); }

u32 cpu_read_mip() {
  u32 x;
  asm volatile("csrr %0, mip" : "=r"(x));
  return x;
}


u32 cpu_get_mode() {
  unsigned int mode;
  asm volatile("csrr %0, mstatus" : "=r"(mode));
  mode = (mode >> 8) & 0x3; //0: User mode 1: Supervisor mode 3: Machine mode
  return mode;
}