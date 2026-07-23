/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "kernel/kernel.h"
#include "ewok.h"

bool _core_proc_ready = false;
int32_t _core_proc_pid = -1;
uint32_t _ipc_uid = 0;

void ewok_fn_init(void** syscall_table) {
  for (int i = 0; i < EWOK_SYS_CALL_NUM; i++) {
    syscall_table[i] = ewok_svc_handler;
  }
}


int ewok_init(void) {
  log_info("ewok module\n");

  sys_fn_init_regist(ewok_fn_init);

  //不同的板子信息不一样
  ewok_sys_info_init();

  return 0;
}

void ewok_exit(void) { log_info("ewok exit\n"); }

module_t ewok_module = {.name = "ewok", .init = ewok_init, .exit = ewok_exit};
