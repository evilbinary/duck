/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "trace.h"

#define NO_INSTRUMENT_FUNCTION __attribute__((__no_instrument_function__))

void NO_INSTRUMENT_FUNCTION __cyg_profile_func_enter(void *func, void *caller) {
  int tid = 0;
  thread_t *current = thread_current();
  if (current != NULL) {
    tid = current->id;
  }
  int ticks = schedule_get_ticks();
  kprintf("[%08d] e tid:%d func:%x caller:%x\n", ticks, tid, func, caller);
}

void NO_INSTRUMENT_FUNCTION __cyg_profile_func_exit(void *func, void *caller) {
  int tid = 0;
  thread_t *current = thread_current();
  if (current != NULL) {
    tid = current->id;
  }
  int ticks = schedule_get_ticks();
  kprintf("[%08d] x tid:%d func:%x caller:%x\n", ticks, tid, func, caller);
}