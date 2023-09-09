/*******************************************************************
* Copyright 2021-present evilbinary
* 作者: evilbinary on 01/01/20
* 邮箱: rootdebug@163.com
********************************************************************/
#include "kernel/kernel.h"



int ipc_init(void) {
  kprintf("ipc init\n");
  return 0;
}

void ipc_exit(void) { kprintf("ipc exit\n"); }


module_t ipc_module = {
    .name ="ipc",
    .init=ipc_init,
    .exit=ipc_exit
};
