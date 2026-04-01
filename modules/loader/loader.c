/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "kernel/kernel.h"
#include "kernel/loader.h"
#include "loader.h"


int loader_init(void) {
  kprintf("loader init arm64=%d\n"),

#if defined(ARM64) || defined(__aarch64__)
  kprintf("loader regist run_elf64_thread=%lx\n", run_elf64_thread);
  loader_regist(&run_elf64_thread);
#else
  kprintf("loader regist run_elf_thread=%lx\n", run_elf_thread);
  loader_regist(&run_elf_thread);
#endif

  return 0;
}

void loader_exit(void) { log_debug("loader exit\n"); }

module_t loader_module = {
    .name = "loader", .init = loader_init, .exit = loader_exit};
