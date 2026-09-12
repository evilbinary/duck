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
#ifndef MP_ENABLE
  if (cpu != 0) {
    for (;;) {
      cpu_wait();
    }
  }
#endif
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
  context_restore(context);
}
