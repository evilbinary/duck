/*******************************************************************
 * Copyright 2021-2080 evilbinary
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

thread_t* schedule_get_next() {
  thread_t* current = thread_current();
  thread_t* next = NULL;
  thread_t* v = thread_head();
  // find next priority
  // if(current_thread->counter<0){
  //   current_thread->counter=0;
  // }
  for (; v != NULL; v = v->next) {
    // kprintf("v %d %d\n",v->id,v->state);
    if (v == current) continue;
    if (v->state != THREAD_RUNNING) continue;
    if (next == NULL) {
      next = v;
    } else if (v->counter <= next->counter) {
      next = v;
    }
  }
  if (next == NULL) {
    next = thread_head();
  }
  next->counter++;
  return next;
}

void schedule(interrupt_context_t* ic) {
  thread_t* current_thread = thread_current();
  int cpu = cpu_get_id();
  schedule_state(cpu);
  thread_t* next_thread = schedule_get_next();
  context_switch(ic, current_thread->ctx, next_thread->ctx);
  context_switch_page(next_thread->vm->upage);
  thread_set_current(next_thread);
  kmemmove(ic, next_thread->ctx->ksp,sizeof(interrupt_context_t));
}

void schedule_next() {
  // while (current_thread == thread_current()) {
  //   cpu_sti();
  // }
  // cpu_cli();
}

void schedule_sleep(u32 nsec) {
  thread_t* current = thread_current();
  u32 tick = nsec / SCHEDULE_FREQUENCY / 1000;
  // kprintf("%d schedule_sleep nsec=%d tick=%d\n", current->id, nsec,tick);
  thread_sleep(current, tick);
}

void schedule_state(int cpu) {
  thread_t* v = thread_head();
  for (; v != NULL; v = v->next) {
    if (v->sleep_counter < 0) {
      thread_wake(v);
    }
    if (v->state == THREAD_SLEEP) {
      u32 ticks = timer_ticks[cpu];
      v->sleep_counter -= ticks - v->counter;
      if (v->sleep_counter <= 0) {
        v->sleep_counter = -1;
      }
    }
  }
}

void* do_schedule(interrupt_context_t* ic) {
  int cpu = cpu_get_id();
  thread_t* next_thread = NULL;
  thread_t* current_thread = thread_current();
  schedule_state(cpu);
  next_thread = schedule_get_next();
  if (next_thread == NULL) {
    kprintf("schedule error next\n");
    return NULL;
  }
  timer_ticks[cpu]++;
  context_switch(ic, current_thread->ctx, next_thread->ctx);
  context_switch_page(next_thread->vm->upage);
  thread_set_current(next_thread);
  timer_end();
  // if (next_thread->id == 2 && next_thread->state ==THREAD_RUNNING) {
  //   int i = 0;
  //   log_debug("next tid %d pc %x pc %x\n",next_thread->id,next_thread->context.ksp->pc,ic->pc);
  // }
  return next_thread->ctx->ksp;
}

void schedule_init() {
  lock_init(&schedule_lock);
  exception_regist(EX_TIMER, do_schedule);
  timer_init(SCHEDULE_FREQUENCY);
}
