/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "exceptions.h"

interrupt_handler_t *exception_handlers[EXCEPTION_NUMBER];
void exception_regist(u32 vec, interrupt_handler_t handler) {
  exception_handlers[vec] = handler;
}

void *exception_process(interrupt_context_t *ic) {
  if (ic->no == EX_OTHER) {
    int cpu = cpu_get_id();
    log_debug("exception cpu %d no %d\n", cpu, ic->no);
    thread_t *current = thread_current();
    if (current != NULL) {
      log_debug("tid:%d %s cpu:%d\n", current->id, current->name,
                current->cpu_id);
    }
  } else if (ic->no == EX_SYS_CALL) {
    thread_t *current = thread_current();
    if (current != NULL) {
      current->ctx->ic = ic; 
      kmemcpy(current->ctx->ksp, ic, sizeof(interrupt_context_t));
    }
  }
  if (exception_handlers[ic->no] != 0) {
    interrupt_handler_t handler = exception_handlers[ic->no];
    if (handler != NULL) {
      return handler(ic);
    }
  }
  return NULL;
}

void exception_process_error(thread_t *current, interrupt_context_t *ic,
                             void *entry) {
  thread_exit(current, -1);

  kprintf("--dump interrupt context--\n");
  context_dump_interrupt(ic);
  kprintf("--dump thread--\n");
  thread_dump(current, DUMP_DEFAULT|DUMP_CONTEXT|DUMP_STACK);

  // set exit handl
  context_set_entry(ic, entry);
  thread_set_entry(current, entry);
  kprintf("exception process error end\n");
}

// in user mode
void exception_error_exit() {
  syscall1(SYS_PRINT, "exception erro exit ^_^!!\n");
  syscall1(SYS_EXIT, 555);
  kprintf("exception exit loop\n");
  for (;;) {
  }
}

void exception_on_permission(interrupt_context_t *ic) {
  int cpu = cpu_get_id();
  log_debug("exception permission on cpu %d no %d code %x\n", cpu, ic->no,
            ic->code);
  thread_t *current = thread_current();
  if (current != NULL) {
    log_debug("tid:%d %s cpu:%d\n", current->id, current->name,
              current->cpu_id);
  }
  exception_process_error(current, ic, (void *)&exception_error_exit);
  // context_dump_interrupt(ic);
  // thread_dump(current);
  // cpu_halt();
}

void exception_on_other(interrupt_context_t *ic) {
  int cpu = cpu_get_id();
  thread_t *current = thread_current();
  log_debug("exception other on cpu %d no %d code %x\n", cpu, ic->no, ic->code);
  exception_info(ic);
  exception_process_error(current, ic, (void *)&exception_error_exit);
}

void exception_on_undef(interrupt_context_t *ic) {
  int cpu = cpu_get_id();
  thread_t *current = thread_current();
  log_debug("exception undef on cpu %d no %d code %x\n", cpu, ic->no, ic->code);
  exception_info(ic);
  kprintf("--dump thread--\n");
  thread_dump(current, DUMP_DEFAULT|DUMP_CONTEXT);
  exception_process_error(current, ic, (void *)&exception_error_exit);
}

void exception_init() {
  interrupt_regist_service(exception_process);

  exception_regist(EX_PERMISSION, exception_on_permission);
  exception_regist(EX_OTHER, exception_on_other);
  exception_regist(EX_PREF_ABORT, exception_on_other);
  exception_regist(EX_RESET, exception_on_other);
  exception_regist(EX_UNDEF, exception_on_undef);
}
