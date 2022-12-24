/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "exceptions.h"

#include "page.h"
#include "thread.h"

interrupt_handler_t *exception_handlers[EXCEPTION_NUMBER];
void exception_regist(u32 vec, interrupt_handler_t handler) {
  exception_handlers[vec] = handler;
}

void *exception_process(interrupt_context_t *ic) {
  if (ic->no == EX_OTHER) {
    int cpu = cpu_get_id();
    kprintf("exception cpu %d no %d\n", cpu, ic->no);
    thread_t *current = thread_current();
    if (current != NULL) {
      kprintf("tid:%d %s cpu:%d\n", current->id, current->name,
              current->cpu_id);
    }
  } else if (ic->no == EX_SYS_CALL ) {
    thread_t *current = thread_current();
    if (current != NULL) {
      current->context.ic=ic;
      kmemmove(current->context.ksp, ic,sizeof(interrupt_context_t));
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

void exception_on_permission(interrupt_context_t *ic) {
  int cpu = cpu_get_id();
  kprintf("exception permission on cpu %d no %d\n", cpu, ic->no);
  thread_t *current = thread_current();
  if (current != NULL) {
    kprintf("tid:%d %s cpu:%d\n", current->id, current->name, current->cpu_id);
  }
  context_dump_interrupt(ic);
  thread_dump();

  cpu_halt();
}

void exception_on_other(interrupt_context_t *ic){
  int cpu = cpu_get_id();
  kprintf("exception other on cpu %d no %d\n", cpu, ic->no);
}

void exception_init() {
  interrupt_regist_service(exception_process);

  exception_regist(EX_PERMISSION, exception_on_permission);
  exception_regist(EX_OTHER, exception_on_other);
  exception_regist(EX_PREF_ABORT, exception_on_other);
  exception_regist(EX_RESET, exception_on_other);

}
