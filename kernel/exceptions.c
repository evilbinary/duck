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

void *exception_process(interrupt_context_t *context) {
  if (context->no == EX_OTHER) {
    int cpu = cpu_get_id();
    kprintf("exception cpu %d no %d\n", cpu, context->no);
    thread_t *current = thread_current();
    if (current != NULL) {
      kprintf("tid:%d %s cpu:%d\n", current->id, current->name,
              current->cpu_id);
    }
  }
  if (exception_handlers[context->no] != 0) {
    interrupt_handler_t handler = exception_handlers[context->no];
    if (handler != NULL) {
      return handler(context);
    }
  }
  return NULL;
}

void exception_init() { interrupt_regist_service(exception_process); }
