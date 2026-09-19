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

  kprintf("cpu_flush_dcache_range not implemented\n");
}

__attribute__((weak)) void cpu_invalidate_dcache_range(unsigned long start,
                                                       unsigned long stop) {
  (void)start;
  (void)stop;
  kprintf("cpu_invalidate_dcache_range not implemented\n");
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

/* 【分页(MMU)是否已开的默认实现（弱符号）】
 * 缺省返回 1（认为已开）：其它架构行为完全不变。armv7-a 在 cpu.c 里给出强实现
 * （读 SCTLR.M），因为 sunxi 这类平台由 U-Boot 引导、进内核时 MMU 还是关的，
 * 此时全部内存都是 Strongly-ordered，ldrex/strex 不可用，打印锁必须退化为普通
 * 自旋（完整现象见 libs/libkernelcommon/io.c）。 */
__attribute__((weak)) int cpu_page_enabled(void) { return 1; }

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

    /* 【打印锁的原子能力跟着 MMU 走】这里读的是硬件事实（armv7-a 读 SCTLR.M；
     * 其它架构由弱符号返回"已开"）：树莓派固件把内核交进来时 MMU 已经开着 → 1；
     * sunxi 由 U-Boot 引导进来时 MMU 还是关的 → 0，内核第一句 kprintf 才不会用
     * ldrex/strex 打死整机（详见 libs/libkernelcommon/io.c）。
     * 必须放在 platform_init() 之前 —— 那里（cpu_clock_init→cpu_get_rate）就有
     * 内核的第一句 kprintf。之后 MMU 真正打开时由 cpu_enable_page() 翻回 1。 */
    io_print_lock_set_atomic(cpu_page_enabled());

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