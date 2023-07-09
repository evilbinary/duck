/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "syscall.h"

static void* syscall_table[SYSCALL_NUMBER] = {0};
sys_fn_handler_fail_t sys_fn_faild_call_fn;

void* do_syscall(interrupt_context_t* ic) {
  // kprintf("syscall %d\n", context_fn(ic));
  if (context_fn(ic) >= 0 && context_fn(ic) < SYSCALL_NUMBER) {
    void* fn = syscall_table[context_fn(ic)];
    if (fn != NULL) {
      // kprintf("syscall fn:%d r0:%x r1:%x r2:%x
      // r3:%x\n",ic->r7,ic->r0,ic->r1,ic->r2,ic->r3);
      sys_fn_call((ic), fn);
      // kprintf(" ret=%x\n",context_ret(ic));
      return context_ret(ic);
    } else {
      log_debug("syscall %d not found\n", context_fn(ic));
    }
  } else if (sys_fn_faild_call_fn != NULL) {
    int ret = sys_fn_faild_call_fn(ic);
    if (ret >= 0) {
      return context_ret(ic);
    }
  } else {
    log_debug("syscall did not found %d\n", context_fn(ic));
  }
  return NULL;
}

void sys_fn_init_regist(sys_fn_handler_t handler) {
  if (handler == NULL) return;
  handler(syscall_table);
}

void sys_fn_init_regist_faild(void* fn) {
  if (fn == NULL) return;
  sys_fn_faild_call_fn = fn;
}

void syscall_init() {
  exception_regist(EX_SYS_CALL, do_syscall);
  sys_fn_init(syscall_table);
}