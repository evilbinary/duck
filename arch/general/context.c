/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "context.h"

#include <pthread.h>
#include <signal.h>
#include "arch/cpu.h"
#include "arch/interrupt.h"
#include "cpu.h"

int context_get_mode(context_t* context) {
  int mode = 0;

  return mode;
}

typedef void* (*fun_fn)();

void* thread_func(void* arg) {
  context_t* context = arg;

  printf("thread %d running flag %d\n", context->tid, context->flag);
  while (context->flag == 0) {
    sleep(1);
  }
  printf("start run %d\n", context->tid);
  fun_fn fun = context->eip;
  fun();
  printf("run end %d\n", context->tid);

  // pthread_exit(NULL);
  return NULL;
}

int context_init(context_t* context, u32* ksp_top, u32* usp_top, u32* entry,
                 u32 level, int cpu) {
  if (context == NULL) {
    return -1;
  }
  log_info("context init\n");

  context->eip = entry;
  context->level = level;

  context->ksp = ksp_top;

  u32 cs, ds;
  // if (level == 0) {
  //   cs = GDT_ENTRY_32BIT_CS * GDT_SIZE;
  //   ds = GDT_ENTRY_32BIT_DS * GDT_SIZE;
  // } else if (level == 3) {
  //   cs = GDT_ENTRY_USER_32BIT_CS * GDT_SIZE | level;
  //   ds = GDT_ENTRY_USER_32BIT_DS * GDT_SIZE | level;
  // } else {
  //   log_error("not suppport level %d\n", level);
  // }

  interrupt_context_t* ic = (u32)ksp_top - sizeof(interrupt_context_t);

  ic->eip = entry;  // eip 4

  ic->no = 0;              // no  5
  ic->code = 0;            // no  5
  ic->eax = 0;             // eax 6
  ic->ecx = 0;             // ecx 7
  ic->edx = 0;             // edx 8
  ic->ebx = 0;             // ebx 9
  ic->esp_null = usp_top;  // esp 10
  ic->ebp = usp_top;       // ebp 11
  ic->esi = 0;             // esi 12
  ic->edi = 0;             // edi 13

  context->ksp = ic;
  // context->ss0 = GDT_ENTRY_32BIT_DS * GDT_SIZE;
  // context->ds0 = GDT_ENTRY_32BIT_DS * GDT_SIZE;
  context->usp = usp_top;
  context->ss = ds;
  context->ds = ds;

  context->attr = kmalloc(sizeof(pthread_attr_t));
  context->thread = kmalloc(sizeof(pthread_t));

  context->flag = 0;

  pthread_attr_init(context->attr);
  if (pthread_create(context->thread, context->attr, thread_func, context) !=
      0) {
    perror("pthread_create error");
    return -1;
  }

  return 0;
}

void context_restore(context_t* current) {
  log_info("context restore %d\n", current->tid);
  current->flag = 1;
}

void context_save(interrupt_context_t* ic, context_t* current) {
  if (ic == NULL) {
    return;
  }
  current->ksp = ic;
}

interrupt_context_t* context_switch(interrupt_context_t* ic, context_t* current,
                                    context_t* next) {
  if (ic == NULL) {
    return ic;
  }
  context_save(ic, current);

  current->flag = 0;
  next->flag = 1;

  return next->ksp;
}

void context_dump(context_t* c) {}

int context_clone(context_t* des, context_t* src) {}

void context_dump_fault(interrupt_context_t* context, u32 fault_addr) {
  kprintf("----------------------------\n");
}
void context_dump_interrupt(interrupt_context_t* context) {}