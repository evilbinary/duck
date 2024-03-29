/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "kernel/kernel.h"
#include "sysfn.h"


int posix_init(void) {
  log_info("posix init\n");

  sys_fn_init();


  return 0;
}

void posix_exit(void) { log_info("posix exit\n"); }

module_t posix_module = {
    .name = "posix", .init = posix_init, .exit = posix_exit};
