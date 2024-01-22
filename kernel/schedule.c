/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "schedule.h"

u32 timer_ticks[MAX_CPU] = {0};
u32 schedule_lock;

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
  thread_t* next = v;
  // find next priority
  for (; v != NULL; v = v->next) {
    if (v->state != THREAD_RUNNING || v == next) {
      continue;
    }
    if (v->counter <= next->counter) {
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
  thread_set_current(next_thread);

  interrupt_context_t* next_ic =
      context_switch(ic, current_thread->ctx, next_thread->ctx);

#ifdef VM_ENABLE
  if (current_thread != next_thread) {
    context_switch_page(next_thread->ctx, next_thread->vm->upage);
  }
#endif
  interrupt_exit_context(next_ic);
}

void schedule_sleep(u32 nsec) {
  thread_t* current = thread_current();
  if (current == NULL || current->state != THREAD_RUNNING) {
    return;
  }
  u32 tick = nsec / SCHEDULE_FREQUENCY / 1000;
  // kprintf("%d schedule_sleep nsec=%d tick=%d\n", current->id, nsec,tick);
  thread_sleep(current, tick);
  // this will fail on qemu memory
  if (current->ctx != NULL && current->ctx->ic != NULL) {
    // schedule(current->ctx->ic);
  }
}

void* do_schedule(interrupt_context_t* ic) {
  int cpu = cpu_get_id();
  thread_t* current_thread = thread_current();
  thread_t* next_thread = current_thread;

  if (current_thread == NULL) {
    log_debug("schedule current is null\n");
    return NULL;
  }

  int count = schedule_state(cpu);

  if (timer_ticks[cpu] % (count * 10) == 1) {
    next_thread = schedule_next(cpu);

    if (next_thread == NULL) {
      log_debug("schedule error next\n");
      return NULL;
    }
  }

  current_thread->counter++;
  current_thread->ticks++;
  timer_ticks[cpu]++;

  if (next_thread->id >= 0) {
    // log_debug("next tid %d\n",next_thread->id);
    //   int i = 0;
    // log_debug("next tid %d ksp %x ksp->pc %x ic->pc %x inst:%x\n",
    //           next_thread->id, next_thread->ctx->ksp,
    //           next_thread->ctx->ksp->sepc, ic->sepc, *(u32*)ic->sepc);
    // log_debug("next tid %d ksp->pc %x ic->pc %x inst:%x\n", next_thread->id,
    //           next_thread->ctx->ksp->pc, ic->pc, *(u32*)ic->pc);

    //   log_debug("next tid %d ksp->pc %x ic->pc %x inst:%x\n",
    //   next_thread->id,
    //             next_thread->ctx->ksp->eip, ic->eip, *(u32*)ic->eip);
    //   log_debug("upage %x ic %x ksp
    //   %x\n",next_thread->vm->upage,ic,next_thread->ctx->ksp);
    //   //  mmu_dump();
  }
  interrupt_context_t* next_ic =
      context_switch(ic, current_thread->ctx, next_thread->ctx);
  thread_set_current(next_thread);
#ifdef VM_ENABLE
  if (current_thread != next_thread) {
    context_switch_page(next_thread->ctx, next_thread->vm->upage);
  }
#endif
  timer_end();
  return next_ic;
}

void schedule_init() {
  // lock_init(&schedule_lock);
  exception_regist(EX_IRQ, do_schedule);
  timer_init(SCHEDULE_FREQUENCY);
  // release(&schedule_lock);
}
