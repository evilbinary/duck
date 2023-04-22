/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "context.h"

#include "cpu.h"

int context_get_mode(context_t* context) {
  int mode = 0;

  return mode;
}

int context_init(context_t* context, u32* ksp_top, u32* usp_top, u32* entry,
                 u32 level, int cpu) {
  if (context == NULL) {
    return;
  }
}

void context_dump(context_t* c) {}

void context_dump_interrupt(interrupt_context_t* ic) {}

void context_dump_fault(interrupt_context_t* context, u32 fault_addr) {}

// #define DEBUG 1
void context_switch(interrupt_context_t* context, context_t** current,
                    context_t* next_context) {
  context_t* current_context = *current;
}

int context_clone(context_t* context, context_t* src) {}
