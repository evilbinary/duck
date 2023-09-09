/*******************************************************************
* Copyright 2021-present evilbinary
* 作者: evilbinary on 01/01/20
* 邮箱: rootdebug@163.com
********************************************************************/
#include "kernel/kernel.h"



int hello_init(void) {
  kprintf("hello init\n");
  return 0;
}

void hello_exit(void) { kprintf("hello exit\n"); }


module_t hello_module = {
    .name ="hello",
    .init=hello_init,
    .exit=hello_exit
};
