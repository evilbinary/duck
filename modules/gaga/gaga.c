/*******************************************************************
* Copyright 2021-2080 evilbinary
* 作者: evilbinary on 01/01/20
* 邮箱: rootdebug@163.com
********************************************************************/
#include "gaga.h"

void gaga_fn_init(void** syscall_table) {
  if(SYS_ADD_CLIENT <SYSCALL_NUMBER){
    syscall_table[SYS_ADD_CLIENT]= &server_add_client;
  }
}

int gaga_init(void) {
  kprintf("gaga init\n");
  
  sys_fn_init_regist(gaga_fn_init);

  return 0;
}

void gaga_exit(void) { kprintf("gaga exit\n"); }


module_t gaga_module = {
    .name ="gaga",
    .init=gaga_init,
    .exit=gaga_exit
};
