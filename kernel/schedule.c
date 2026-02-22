/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "schedule.h"

u32 timer_ticks[MAX_CPU] = {0};
lock_t schedule_lock;

u32 schedule_get_ticks() {
  int cpu = cpu_get_id();
  return timer_ticks[cpu];
}

int schedule_state(int cpu) {
  int count = 0;
  thread_t* v = thread_head();
  for (; v != NULL; v = v->next) {
    if (v->state == THREAD_SLEEP) {
      u32 ticks = timer_ticks[cpu];
      v->sleep_counter--;
      if (v->sleep_counter <= 0) {
        thread_wake(v);
      }
    } else if (v->state == THREAD_RUNNING) {
      count++;
    }
  }
  return count;
}

thread_t* schedule_next(int cpu) {
  thread_t* current = thread_current();
  thread_t* v = thread_head();
  thread_t* next = NULL;
  
  // find first runnable thread
  for (; v != NULL; v = v->next) {
    if (v->state == THREAD_RUNNING && v != current) {
      next = v;
      break;
    }
  }
  
  // if no other runnable thread, return current if it's runnable
  if (next == NULL) {
    if (current != NULL && current->state == THREAD_RUNNING) {
      return current;
    }
    return NULL;
  }
  
  // find thread with lowest counter (highest priority)
  for (v = thread_head(); v != NULL; v = v->next) {
    if (v->state != THREAD_RUNNING || v == current) {
      continue;
    }
    if (v->counter < next->counter) {
      next = v;
    }
  }

  return next;
}

void schedule(interrupt_context_t* ic) {
  thread_t* current_thread = thread_current();
  int cpu = cpu_get_id();
  schedule_state(cpu);
  thread_t* next_thread = schedule_next(cpu);
  
  // if no next thread or same as current, don't switch
  if (next_thread == NULL || next_thread == current_thread) {
    return;
  }
  
  interrupt_context_t* next_ic =
      context_switch(ic, current_thread->ctx, next_thread->ctx);
  thread_set_current(next_thread);
#ifdef VM_ENABLE
  context_switch_page(next_thread->ctx, next_thread->vm->upage);
#endif
}

void schedule_switch() {
  thread_t* current_thread = thread_current();
  interrupt_context_t* ic = current_thread->ctx->ic;
  int cpu = cpu_get_id();
  schedule_state(cpu);
  thread_t* next_thread = schedule_next(cpu);
  
  // if no next thread or same as current, just exit
  if (next_thread == NULL || next_thread == current_thread) {
    interrupt_exit_context(ic);
    return;
  }
  
  thread_set_current(next_thread);

  interrupt_context_t* next_ic =
      context_switch(ic, current_thread->ctx, next_thread->ctx);

#ifdef VM_ENABLE
  context_switch_page(next_thread->ctx, next_thread->vm->upage);
#endif
  interrupt_exit_context(next_ic);
}

void schedule_sleep(u32 nsec) {
  thread_t* current = thread_current();
  if (current == NULL || current->state != THREAD_RUNNING) {
    return;
  }
  u32 tick = nsec / SCHEDULE_FREQUENCY;
  // kprintf("%d schedule_sleep nsec=%d tick=%d\n", current->id, nsec,tick);
  thread_sleep(current, tick);
  // thread_dumps();

  // this will fail on qemu memory
  if (current->ctx != NULL && current->ctx->ic != NULL) {
    // schedule(current->ctx->ic);
  }
}

void* do_schedule(interrupt_context_t* ic) {
  int cpu = cpu_get_id();
  thread_t* current_thread = thread_current();

  if (current_thread == NULL) {
    log_debug("schedule current is null\n");
    return ic;
  }

  int count = schedule_state(cpu);

  thread_t* next_thread = schedule_next(cpu);
  if (next_thread == NULL) {
    log_debug("schedule error next\n");
    timer_end();
    return ic;
  }
  
  // if same thread, just update stats and return
  if (next_thread == current_thread) {
    next_thread->counter++;
    next_thread->ticks++;
    timer_ticks[cpu]++;
    timer_end();
    return ic;
  }

  next_thread->counter++;
  next_thread->ticks++;
  timer_ticks[cpu]++;
  interrupt_context_t* next_ic =
      context_switch(ic, current_thread->ctx, next_thread->ctx);
  thread_set_current(next_thread);
#ifdef VM_ENABLE
  context_switch_page(next_thread->ctx, next_thread->vm->upage);
#endif
  timer_end();
  return next_ic;
}

void schedule_init() {
  exception_regist(EX_TIMER, do_schedule);
  timer_init(SCHEDULE_FREQUENCY);
}
