/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "cpu.h"

#include "context.h"

extern boot_info_t* boot_info;

void cpu_init() {}

void cpu_halt() {
  for (;;) {
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

void cpu_backtrace(void) {}

void cpu_set_page(u32 page_table) {}

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
  // asm volatile("csrr %0, mcause" : "=r"(fault));
  // 返回值
  return fault;
}