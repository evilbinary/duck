/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "preempt.h"

#include "schedule.h"
#include "thread.h"

int preempt_model = PREEMPT_MODEL_DEFAULT;

static u32 preempt_count[MAX_CPU];
static u8 need_resched[MAX_CPU];

void preempt_init(void) {
  int i;
  for (i = 0; i < MAX_CPU; i++) {
    preempt_count[i] = 0;
    need_resched[i] = 0;
  }
  preempt_model = PREEMPT_MODEL_DEFAULT;
}

void preempt_set_model(int model) {
  if (model < PREEMPT_NONE) {
    model = PREEMPT_NONE;
  }
  if (model > PREEMPT_RT) {
    model = PREEMPT_RT;
  }
  preempt_model = model;
}

int preempt_get_model(void) { return preempt_model; }

void preempt_disable(void) {
  int cpu = cpu_get_id();
  if (cpu < 0 || cpu >= MAX_CPU) {
    return;
  }
  preempt_count[cpu]++;
}

void preempt_enable(void) {
  int cpu = cpu_get_id();
  if (cpu < 0 || cpu >= MAX_CPU) {
    return;
  }
  if (preempt_count[cpu] == 0) {
    return;
  }
  preempt_count[cpu]--;
  if (preempt_count[cpu] == 0) {
    preempt_check_resched();
  }
}

/* 上下文已切换到别的线程后，清掉本核的抢占计数/重调度标志：
 * 退出临界区（thread_stop 后的 preempt_disable）是"针对要退出的那个线程"的，
 * 切到新线程后新线程本就可抢占，不能把计数留给它。 */
void preempt_reset(void) {
  int cpu = cpu_get_id();
  if (cpu < 0 || cpu >= MAX_CPU) {
    return;
  }
  preempt_count[cpu] = 0;
  need_resched[cpu] = 0;
}

int preempt_count_get(void) {
  int cpu = cpu_get_id();
  if (cpu < 0 || cpu >= MAX_CPU) {
    return 1;
  }
  return (int)preempt_count[cpu];
}

void preempt_set_need_resched(void) {
  int cpu = cpu_get_id();
  if (cpu < 0 || cpu >= MAX_CPU) {
    return;
  }
  need_resched[cpu] = 1;
}

void preempt_clear_need_resched(void) {
  int cpu = cpu_get_id();
  if (cpu < 0 || cpu >= MAX_CPU) {
    return;
  }
  need_resched[cpu] = 0;
}

int preempt_need_resched(void) {
  int cpu = cpu_get_id();
  if (cpu < 0 || cpu >= MAX_CPU) {
    return 0;
  }
  return need_resched[cpu] != 0;
}

int preempt_may_switch(interrupt_context_t* ic) {
  if (preempt_count_get() != 0) {
    return 0;
  }
  /* 嵌套硬中断栈上禁止切换 */
  if (!context_irq_preemptible(ic)) {
    return 0;
  }
  /* NONE/VOLUNTARY：打断的是 SVC(syscall) 则只记账，回用户/显式点再切。
   * FULL/RT：允许在 SVC 路径抢占（preempt_count==0）。
   * SYS 上的内核线程在各模式下仍允许时钟切换。
   * 【armv5 特例】SVC bank SP 是全局共享的 _stack_svc（context_switch 并不切
   * r13_svc），一旦在 SVC 里被时钟换出，内核调用链会被下一个进入 SVC 的线程
   * 覆盖，恢复即崩（实测：init 下一次 SVC 在 svc_handler 的 stmdb 处 data abort）。
   * 所以 armv5 上与其它架构一致：SVC 中不抢占。阻塞型系统调用不能靠"SVC 被抢占"
   * 让出 CPU，只能显式 schedule_switch()（且不再返回内核调用链）。 */
  if (context_in_kernel(ic) && preempt_model < PREEMPT_FULL) {
    return 0;
  }
  return 1;
}

interrupt_context_t* preempt_on_return_user(interrupt_context_t* ic) {
  if (ic == NULL) {
    return ic;
  }
  if (!preempt_need_resched()) {
    return ic;
  }
  if (preempt_count_get() != 0) {
    return ic;
  }
  return schedule_reschedule(ic);
}

void cond_resched(void) {
  if (preempt_model < PREEMPT_VOLUNTARY) {
    return;
  }
  if (preempt_count_get() != 0) {
    return;
  }
  /* KISS：YiYiYa 的 context_switch 依赖中断帧，任意内核点直接切不安全。
   * 这里只请求调度，由时钟 IRQ 出口或 syscall 回用户完成真正切换。 */
  preempt_set_need_resched();
}

void preempt_check_resched(void) {
  if (preempt_model < PREEMPT_FULL) {
    return;
  }
  if (!preempt_need_resched()) {
    return;
  }
  if (preempt_count_get() != 0) {
    return;
  }
  /* FULL 下中段切线程仍依赖合法中断/syscall 帧；无 ic 则等出口。
   * 真正的内核抢占由 do_schedule（IRQ 嵌套返回）完成。 */
}
