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
      /* YiYiYa: 【不再按 cpu_id 过滤】——原意是防 SMP 下 N 倍唤醒，但 miyoo
       * 实测【只有 CPU0 有定时器 tick】（boot 打印 ticks=(N,0,0,0) ✓；AP 没
       * 起来/没 arm 定时器 ✗），而线程的 cpu_id 可能是 1/2/3（创建时所在核 ✗）
       * ⇒ 它们的 sleep_counter 永远不递减 ⇒ "睡"只能靠事件唤醒 ✗：SDL_Delay(50)
       * 实测睡 ~1.5 秒、手柄要等事件才有反应 ✗✗。当前实际只有单核在 tick ⇒
       * 直接递减才对 ✓（将来若 AP 各自 tick，需改成"绝对到期时刻 + 单核负责"）。 */
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
    thread_bind_finish(current_thread, cpu);
    preempt_reset(); /* 退出临界区的抢占计数不留给新线程 */
#ifdef VM_ENABLE
    context_switch_page(next_thread->ctx, next_thread->vm->upage);
#endif
    thread_recycle_process(); /* 已切到别的线程，此时回收退出的线程是安全的 */
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
  thread_bind_finish(current_thread, cpu);
  preempt_reset(); /* 退出临界区的抢占计数不留给新线程 */
#ifdef VM_ENABLE
  context_switch_page(next_thread->ctx, next_thread->vm->upage);
#endif
  thread_recycle_process(); /* 已切到别的线程，此时回收退出的线程是安全的 */
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
  thread_bind_finish(current_thread, cpu);
  preempt_reset(); /* 退出临界区的抢占计数不留给新线程 */

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

void schedule_sleep(u32 ticks) {
  thread_t* current = thread_current();
  /* 【关键修复】原先只允许 THREAD_RUNNING：
   * 线程在 SVC（系统调用）执行期间，可能已被时钟中断标成 THREAD_RUNABLE
   * （时间片到期/被抢占），此时它来请求睡眠就被直接丢掉 ⇒ 睡眠形同空操作。
   * 实测症状：应用/线程在 usleep 循环里全速空转，帧率与音频生产速率完全不受控
   * （游戏偏快 1.35 倍、音频每秒多产 17kHz ⇒ 环形缓冲反复溢出"滋滋"）。
   * RUNABLE(2) 同样属于"可运行"，请求睡眠是合法的，必须受理；
   * 只有确实已经睡着/等待/停止的线程才应忽略。 */
  if (current == NULL || (current->state != THREAD_RUNNING &&
                          current->state != THREAD_RUNABLE)) {
    return;
  }
  /* Argument is already timer ticks (see SECOND_TO_TICK / NANOSECOND_TO_TICK).
   * Do not divide by SCHEDULE_FREQUENCY again — that made 500ms → 1ms and
   * left init polling serial at ~1kHz (high cpu%). */
  u32 tick = ticks;
  if (tick == 0) {
    tick = 1;
  }

  /* 【KISS 修法】只把线程置成 SLEEP 就返回 —— 绝不在内核态睡眠/让出。
   *
   * 为什么：本内核"运行帧"放在共享栈区、线程内核栈只放保存帧。一旦在内核态
   * （例如 sys_select 里调用本函数）被时钟换出，该线程的内核调用链就留在共享栈
   * 上；下一个线程在同一片区域跑内核代码就会把它覆盖 ⇒ 线程恢复后一读自己栈上
   * 的局部变量就是别人的数据 ⇒ 立刻崩。
   * 实测：t113-s3 必崩（单核，挂起后必定轮到别的线程用同一片栈）；QEMU 有 4 核、
   * 一个核上常只有 1 个可运行线程，被换出后往往又是它自己回来 ⇒ 侥幸不崩。
   *
   * 这也正是 t113-s3-lcd1（QEMU 与 t113-s3 都好的那支）的行为：thread_sleep()
   * 之后直接返回，真正的换出在那里是被注释掉的。线程很快回到用户态，只会在
   * 【用户态边界】被换出，因而安全 —— 等待交给调用者在用户态轮询。
   *
   * 注意：若将来要真正的阻塞式 select，必须先做"每线程内核栈 + 入口/出口成对
   * 切栈"的结构改造，不能只在切换点/这里打补丁（已实测过两种打补丁都失败）。 */
  thread_sleep(current, tick);
}

void* do_schedule(interrupt_context_t* ic) {
  int cpu = cpu_get_id();
  thread_t* current_thread = thread_current();

  if (current_thread == NULL) {
    /* 【必须 ack 定时器 —— 这就是"早期 tick 风暴"的根因，见设计文档 §11.1-2】
     * 本函数由 tick（走 EX_TIMER 槽）调用。内核早期（中断控制器已注册、但
     * thread_init 之前）current_thread 还不存在；如果这里直接 return，
     * **定时器中断永远不被清** ⇒ 自动重装的定时器立刻重投递 ⇒ IRQ 风暴 +
     * 启动停摆（"schedule current is null" 刷屏，再也进不了 shell）。
     * raspi3 切到统一中断框架后实测：chip 注册的那一瞬 tick 就来了，日志被刷
     * 了 644 行后卡死。所以没有当前线程时也要 timer_end()（只写定时器寄存器，
     * 不需要线程上下文），并让 tick 计数继续走 —— 但不切换上下文。 */
    timer_end();
    timer_ticks[cpu]++;
    return ic;
  }

  schedule_state(cpu);
  timer_ticks[cpu]++;
  /* Only RUNNING burns "busy" time. SLEEP/WAITING still current during
   * nanosleep/wait in SVC would otherwise steal all ticks from children. */
  if (current_thread->state == THREAD_RUNNING) {
    current_thread->ticks++;
  }

  {
    int blocked = (current_thread->state == THREAD_SLEEP ||
                   current_thread->state == THREAD_WAITING);
    /* 【KISS 修法】去掉这里的 `!blocked &&`：睡眠/等待中的线程同样必须遵守
     * "内核态（SVC=正在 syscall）不得换出"这条不变量。
     *
     * 保留 !blocked 的后果（t113-s3-lcd 分支实测）：sleep 在 syscall 里被时钟
     * 换出 ⇒ 内核调用链留在共享栈上，被下一个线程覆盖 ⇒ 恢复即崩（t113 单核必
     * 崩；QEMU 4 核常又切回自己而侥幸不崩）。
     * 判定沿用 preempt_may_switch()：它只对 SVC(0x13) 返回 0（见 context_in_kernel
     * 的注释），SYS/内核线程仍可被时钟切换，不会饿死 init。
     * 配套：schedule_sleep() 已改为只置 SLEEP 就返回（不在内核态等），于是睡眠
     * 线程会很快回到用户态，在用户态边界再被换出 —— 与 t113-s3-lcd1（两支都好）
     * 的行为一致。 */
    if (!preempt_may_switch(ic)) {
      preempt_set_need_resched();
      timer_end();
      return ic;
    }

    thread_t* next_thread = schedule_next(cpu);
    if (next_thread == NULL) {
      if (!blocked) {
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
