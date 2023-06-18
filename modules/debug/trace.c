/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "trace.h"

#define NO_INSTRUMENT_FUNCTION __attribute__((__no_instrument_function__))

int enable_pmu = 0;
int trace_level = -1;

void NO_INSTRUMENT_FUNCTION __cyg_profile_func_enter(void *func, void *caller) {
  int tid = 0;
  thread_t *current = thread_current();
  if (current != NULL) {
    tid = current->id;
  }
  if (enable_pmu == 0) {
    cpu_pmu_enable();
    enable_pmu = 1;
  }
  trace_level++;
  u32 ticks = cpu_cyclecount();
  kprintf("[%010d] e t:%d f:%x c:%x l:%d\n", ticks, tid, func, caller,
          trace_level);
}

void NO_INSTRUMENT_FUNCTION __cyg_profile_func_exit(void *func, void *caller) {
  int tid = 0;
  thread_t *current = thread_current();
  if (current != NULL) {
    tid = current->id;
  }

  u32 ticks = cpu_cyclecount();
  kprintf("[%08d] x t:%d f:%x c:%x l:%d\n", ticks, tid, func, caller,trace_level);
  trace_level--;
}