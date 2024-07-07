/*******************************************************************
* Copyright 2021-present evilbinary
* 作者: evilbinary on 01/01/20
* 邮箱: rootdebug@163.com
********************************************************************/
#include "kernel/kernel.h"



int power_init(void) {
  kprintf("power init\n");
  return 0;
}

void power_exit(void) { kprintf("power exit\n"); }


module_t power_module = {
    .name ="power",
    .init=power_init,
    .exit=power_exit
};
