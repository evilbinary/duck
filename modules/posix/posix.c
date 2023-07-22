/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "kernel/kernel.h"
#include "sysfn.h"

int posix_init(void) {
  kprintf("posix init\n");

  sys_fn_init_regist(sys_fn_init);

  return 0;
}

void posix_exit(void) { kprintf("posix exit\n"); }

module_t posix_module = {
    .name = "posix", .init = posix_init, .exit = posix_exit};
