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

/* 【页清零的默认实现（弱符号）】
 * PIPT / 硬件自动一致的架构直接 memset 即可：同一物理页在不同 VA 下是同一
 * cache 行，不存在别名分叉。armv5(VIVT) 在 cpu.c 里给出强实现（memset +
 * clean+invalidate），链接时自动覆盖本弱定义。 */
__attribute__((weak)) void cpu_zero_phy_page(void* p, unsigned long size) {
  if (p != NULL && size > 0) {
    kmemset(p, 0, size);
  }
}

/* 【统一 TLS 接口的默认实现（弱符号）】
 * 只有 ARMv6K/ARMv7+ 才有 TPIDRURO；armv5(ARM926EJ-S)、ARMv7-M(cortex-m4)、
 * x86、riscv 都没有该寄存器，空实现即可（线程指针由 current->user_tp /
 * sys_thread_self() 维护）。armv7-a 在 cpu.c 里给出强实现覆盖本定义。 */
__attribute__((weak)) void cpu_set_tls(void* tp) { (void)tp; }

/* 【缓存自测的默认实现（弱符号）】
 * 只有实现了时基/PMU 的架构才给强实现（当前 armv5）。其它架构空实现。 */
__attribute__((weak)) void cpu_cache_selftest(void) {}

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