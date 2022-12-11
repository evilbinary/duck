/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "syscall.h"

static void* syscall_table[SYSCALL_NUMBER] = {0};

void* do_syscall(interrupt_context_t* ic) {
  // kprintf("syscall %d\n", context_fn(context));
  if (context_fn(ic) >= 0 && context_fn(ic) < SYSCALL_NUMBER) {
    void* fn = syscall_table[context_fn(ic)];
    if (fn != NULL) {
      // kprintf("syscall fn:%d r0:%x r1:%x r2:%x
      // r3:%x\n",context->r7,context->r0,context->r1,context->r2,context->r3);
      sys_fn_call((ic), fn);
      // kprintf(" ret=%x\n",context_ret(context));
      return context_ret(ic);
    } else {
      kprintf("syscall %d not found\n", context_fn(ic));
    }
  }
  return NULL;
}


void syscall_init() {
  exception_regist(EX_SYS_CALL, do_syscall);
  sys_fn_init(syscall_table);
}