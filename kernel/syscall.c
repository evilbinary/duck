/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "syscall.h"

static void** syscall_table = NULL;
int syscall_total = 0;

sys_fn_handler_fail_t syscall_faild_handler_fn = NULL;
sys_fn_handler_t syscall_handler_fn = NULL;

void* do_syscall(interrupt_context_t* ic) {
  // kprintf("syscall %d\n", context_fn(ic));
  int no = context_fn(ic);
  if (no >= 0 && no < SYSCALL_NUMBER && syscall_handler_fn != NULL) {
    syscall_handler_fn(no, ic);
    return context_ret(ic);
  } else if (syscall_faild_handler_fn != NULL) {
    int ret = syscall_faild_handler_fn(no, ic);
    if (ret >= 0) {
      return context_ret(ic);
    }
  } else {
    log_warn("syscall did not found %d\n", context_fn(ic));
  }
  return NULL;
}

void sys_fn_regist_faild(void* fn) {
  if (fn == NULL) return;
  syscall_faild_handler_fn = fn;
}

void sys_fn_regist_handler(void* fn) {
  if (fn == NULL) return;
  syscall_handler_fn = fn;
}

void* sys_fn_get_handler() { return syscall_handler_fn; }

void syscall_init() { exception_regist(EX_SYS_CALL, do_syscall); }