/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "kernel/kernel.h"
#include "loader.h"


int loader_init(void) {
  log_debug("loader init\n");

  loader_regist(&run_elf_thread);

  return 0;
}

void loader_exit(void) { log_debug("loader exit\n"); }

module_t loader_module = {
    .name = "loader", .init = loader_init, .exit = loader_exit};
