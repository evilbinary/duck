/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "schedule.h"

#include "preempt.h"

u32 timer_ticks[MAX_CPU] = {0};
lock_t schedule_lock;

u32 schedule_get_ticks() {
  int cpu = cpu_get_id();
  return timer_ticks[cpu];
}

u32 schedule_get_ticks_cpu(int cpu) {
  if (cpu < 0 || cpu >= MAX_CPU) {
    return 0;
  }
  return timer_ticks[cpu];
}

int schedule_state(int cpu) {
  int count = 0;
  thread_t* v = thread_head();
  for (; v != NULL; v = v->next) {
    if (v->state == THREAD_SLEEP) {
      /* Only the thread's CPU advances its sleep; otherwise SMP wakes Nx faster. */
      if (v->cpu_id == cpu) {
        v->sleep_counter--;
        if (v->sleep_counter <= 0) {
          thread_wake(v);
        }
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

  for (; v != NULL; v = v->next) {
    if (v->state == THREAD_RUNNING && v->cpu_id == cpu && v != current) {
      next = v;
      break;
    }
  }

  if (next == NULL) {
    if (current != NULL && current->state == THREAD_RUNNING &&
        current->cpu_id == cpu) {
      return current;
    }
    return NULL;
  }

  /* 先比 priority（越小越高），再比 counter */
  for (v = thread_head(); v != NULL; v = v->next) {
    if (v->state != THREAD_RUNNING || v->cpu_id != cpu || v == current) {
      continue;
    }
    if (v->priority < next->priority ||
        (v->priority == next->priority && v->counter < next->counter)) {
      next = v;
    }
  }

  return next;
}

interrupt_context_t* schedule_reschedule(interrupt_context_t* ic) {
  int cpu = cpu_get_id();
  thread_t* current_thread = thread_current();
  thread_t* next_thread;

  if (current_thread == NULL || ic == NULL) {
    return ic;
  }

  next_thread = schedule_next(cpu);
  if (next_thread == NULL) {
    preempt_clear_need_resched();
    return ic;
  }

  if (next_thread == current_thread) {
    next_thread->counter++;
    preempt_clear_need_resched();
    return ic;
  }

  next_thread->counter++;
  preempt_clear_need_resched();
  {
    interrupt_context_t* next_ic =
        context_switch(ic, current_thread->ctx, next_thread->ctx);
    thread_set_current(next_thread);
#ifdef VM_ENABLE
    context_switch_page(next_thread->ctx, next_thread->vm->upage);
#endif
    return next_ic;
  }
}

void schedule(interrupt_context_t* ic) {
  thread_t* current_thread = thread_current();
  int cpu = cpu_get_id();
  schedule_state(cpu);
  thread_t* next_thread = schedule_next(cpu);

  if (next_thread == NULL || next_thread == current_thread) {
    return;
  }

  interrupt_context_t* next_ic =
      context_switch(ic, current_thread->ctx, next_thread->ctx);
  thread_set_current(next_thread);
#ifdef VM_ENABLE
  context_switch_page(next_thread->ctx, next_thread->vm->upage);
#endif
  (void)next_ic;
}

void schedule_switch() {
  thread_t* current_thread = thread_current();
  interrupt_context_t* ic = current_thread->ctx->ic;
  int cpu = cpu_get_id();
  schedule_state(cpu);
  thread_t* next_thread = schedule_next(cpu);

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

static int schedule_runnable_on_cpu(int cpu) {
  int count = 0;
  thread_t* v = thread_head();
  for (; v != NULL; v = v->next) {
    if (v->state == THREAD_RUNNING && v->cpu_id == cpu) {
      count++;
    }
  }
  return count;
}

void schedule_sleep(u32 nsec) {
  thread_t* current = thread_current();
  if (current == NULL || current->state != THREAD_RUNNING) {
    return;
  }
  u32 tick = nsec / SCHEDULE_FREQUENCY;
  if (tick == 0) {
    tick = 1;
  }

  /*
   * KERNEL/SYS-mode threads (kernel, monitor) must not use THREAD_SLEEP:
   * leaving RUNNING + context_switch on raspi2 zeros SYS SP → fault at 0x1 →
   * thread_exit → ps "stopped". Same for sole-runnable idle CPUs.
   * Park with WFI until this CPU's timer_ticks advance.
   */
  {
    int cpu = cpu_get_id();
    int kernel_thread =
        (current->level == LEVEL_KERNEL ||
         current->level == LEVEL_KERNEL_SHARE);
    if (kernel_thread || schedule_runnable_on_cpu(cpu) <= 1) {
      u32 start = timer_ticks[cpu];
      while (timer_ticks[cpu] - start < tick) {
        cpu_wait();
      }
      return;
    }
  }

  thread_sleep(current, tick);
  while (current->state == THREAD_SLEEP) {
    cpu_wait();
  }
}

void* do_schedule(interrupt_context_t* ic) {
  int cpu = cpu_get_id();
  thread_t* current_thread = thread_current();

  if (current_thread == NULL) {
    log_debug("schedule current is null\n");
    return ic;
  }

  schedule_state(cpu);
  current_thread->ticks++;
  timer_ticks[cpu]++;

  if (!preempt_may_switch(ic)) {
    preempt_set_need_resched();
    timer_end();
    return ic;
  }

  {
    thread_t* next_thread = schedule_next(cpu);
    if (next_thread == NULL) {
      /* Sleeping/waiting on an otherwise-idle CPU is normal (AP monitor). */
      if (current_thread->state != THREAD_SLEEP &&
          current_thread->state != THREAD_WAITING) {
        log_debug("schedule error next\n");
        thread_t* v = thread_head();
        for (; v != NULL; v = v->next) {
          kprintf("TS tid=%d state=%d sleep=%d counter=%d cpu=%d name=%s\n",
                  v->id, v->state, v->sleep_counter, v->counter, v->cpu_id,
                  v->name != NULL ? v->name : "null");
        }
      }
      timer_end();
      return ic;
    }

    if (next_thread == current_thread) {
      next_thread->counter++;
      preempt_clear_need_resched();
      timer_end();
      return ic;
    }

    {
      interrupt_context_t* next_ic = schedule_reschedule(ic);
      timer_end();
      return next_ic;
    }
  }
}

void schedule_init() {
  preempt_init();
  if (cpu_get_id() == 0) {
    exception_regist(EX_TIMER, do_schedule);
  }
  timer_init(SCHEDULE_FREQUENCY);
}
