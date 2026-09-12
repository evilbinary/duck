/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "arch.h"
#include "context.h"
#include "kernel/string.h"

boot_info_t* boot_info = NULL;

/* 【统一 cache 接口的默认实现（弱符号）】
 * 供"不需要 cache 维护 / 硬件自动保持一致"的架构兜底（x86、riscv、lx6、
 * dummy、general）。armv7-a / armv7 / armv5 / armv8-a 在各自 cpu.c 里给出
 * 强实现，链接时自动覆盖弱定义。 */
__attribute__((weak)) void cpu_flush_dcache_range(unsigned long start,
                                                  unsigned long stop) {
  (void)start;
  (void)stop;
}

__attribute__((weak)) void cpu_invalidate_dcache_range(unsigned long start,
                                                       unsigned long stop) {
  (void)start;
  (void)stop;
}

__attribute__((weak)) void cpu_invalidate_icache(void) {}

void context_inherit_live(context_t* child, interrupt_context_t* live) {
  if (child == NULL || live == NULL) {
    return;
  }
  interrupt_context_t* slot = child->ksp;
  if (slot == NULL) {
    return;
  }
  kmemmove(slot, live, sizeof(interrupt_context_t));
  child->ic = slot;
  child->ksp = slot;
}
extern u32 write_channel_number;

void arch_init(boot_info_t* boot, int cpu) {
  if (cpu == 0) {
    boot_info = boot;
    write_channel_number = 0;
    platform_init();

    cpu_init(cpu);
    display_init();
    interrupt_init(cpu);

    mm_init();
    platform_end();
  } else {
#ifdef MP_ENABLE
    ap_init(cpu);
#endif
  }
}