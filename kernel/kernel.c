/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/

#include "kernel.h"
#include "page.h"

#include "config.h"

void kernel_init() {
  int cpu = cpu_get_id();
  if (cpu == 0) {
    log_init();
    /* 【版本戳·无条件一行】确认"烧的是哪一次编译"，并打印本架构内核内存实际
     * 解析到的属性值：armv7-a 期望 0xc（TEX=000,C=1,B=1 = WB 可缓存）；
     * 若为 0x4c（旧 Reserved 编码）或 0x0（Strongly-ordered）说明固件是旧包。
     * 页属性/带宽诊断探针见 -DMM_DEBUG_PROBE=1。 */
    kprintf("BUILD_TAG %s %s attrs=%x\n", __DATE__, __TIME__,
            (u32)PAGE_KERNEL);
    log_info("kernel init\n");
    log_info("log init\n");
    log_info("exception init\n");
    exception_init();
    log_info("page init\n");
    page_init();
    log_info("syscall init\n");
    syscall_init();
    log_info("schedule init\n");
    schedule_init();
    log_info("module init\n");
    module_init();
    log_info("memory init\n");
    memory_init();
#if MM_DEBUG_PROBE && defined(ARMV7_A)
    /* 页属性/带宽自测（-DMM_DEBUG_PROBE=1 时启用）：MEMATTR 里 l2e 低 12 位
     * 应为 0x43e（= 0x432 | 0xC，WB 可缓存）；MEMBW 4KB ≈ 0.22 B/cyc 为
     * t113 可缓存参考值（非缓存 Strongly-ordered 时仅 ~0.004）。 */
    cpu_mem_bw_test();
#endif
#if MM_DEBUG_PROBE && defined(ARMV5)
    /* ARMv5(ARM926) 缓存自测：ARM926 无 PMU，改用调度 tick 计时，比对
     * 4KB(可常驻 D-cache) / 256KB(远超 D-cache) 的读带宽，并回读 SCTLR
     * 的 M/C/W/I 位，用来判定 cache 是否真的开起来了。 */
    cpu_cache_selftest();
#endif
    log_info("vfs init\n");
    vfs_init();
    log_info("thread init\n");
    thread_init();
    log_info("event init\n");
    event_init();
    log_info("kernel init end\n");
  } else {
    kprintf("ap %d kernel init\n", cpu);
    page_init();
    schedule_init();
    kprintf("ap %d kernel init end\n", cpu);
  }
}

void kernel_run() {
  context_t* context = thread_current_context();
  if (context == NULL) {
    log_error("kernel_run: context NULL (no current thread)\n");
  }
  context_restore(context);
}
