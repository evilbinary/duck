/*******************************************************************
* Copyright 2021-2080 evilbinary
* 作者: evilbinary on 01/01/20
* 邮箱: rootdebug@163.com
********************************************************************/
#include "kernel/kernel.h"



int musl_init(void) {
  kprintf("Hello World\n");

  
  return 0;
}

void musl_exit(void) { kprintf("musl exit\n"); }


module_t musl_module = {
    .name ="musl",
    .init=musl_init,
    .exit=musl_exit
};
