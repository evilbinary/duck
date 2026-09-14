/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "rt_mutex.h"

#include "preempt.h"

#ifndef cpu_wait
void cpu_wait(void);
#endif

void schedule_switch(void);

static void rt_mutex_pi_boost(rt_mutex_t* m, thread_t* waiter) {
  thread_t* owner;

  if (m == NULL || waiter == NULL) {
    return;
  }
  owner = m->owner;
  if (owner == NULL || owner == waiter) {
    return;
  }
  /* 数值更小 = 更高优先级 */
  if (waiter->priority < owner->priority ||
      (waiter->priority == owner->priority &&
       waiter->counter < owner->counter)) {
    if (m->pi_active == 0) {
      m->owner_priority_saved = owner->priority;
      m->owner_counter_saved = owner->counter;
      m->pi_active = 1;
    }
    if (waiter->priority < owner->priority) {
      owner->priority = waiter->priority;
    }
    if (waiter->counter < owner->counter) {
      owner->counter = waiter->counter;
    }
  }
}

static void rt_mutex_pi_restore(rt_mutex_t* m) {
  if (m == NULL || m->pi_active == 0 || m->owner == NULL) {
    return;
  }
  m->owner->priority = m->owner_priority_saved;
  m->owner->counter = m->owner_counter_saved;
  m->pi_active = 0;
}

static int rt_mutex_waiter_on_list(rt_mutex_t* m, thread_t* t) {
  thread_t* w;

  for (w = m->wait_head; w != NULL; w = w->rt_wait_next) {
    if (w == t) {
      return 1;
    }
  }
  return 0;
}

static void rt_mutex_enqueue(rt_mutex_t* m, thread_t* t) {
  thread_t** pp;
  thread_t* w;

  if (rt_mutex_waiter_on_list(m, t)) {
    return;
  }
  t->rt_wait_next = NULL;
  /* 按 priority、counter 插入，高优先级靠前 */
  pp = &m->wait_head;
  for (w = m->wait_head; w != NULL; w = w->rt_wait_next) {
    if (t->priority < w->priority ||
        (t->priority == w->priority && t->counter < w->counter)) {
      break;
    }
    pp = &w->rt_wait_next;
  }
  t->rt_wait_next = *pp;
  *pp = t;
}

static thread_t* rt_mutex_dequeue_best(rt_mutex_t* m) {
  thread_t* t;

  t = m->wait_head;
  if (t == NULL) {
    return NULL;
  }
  m->wait_head = t->rt_wait_next;
  t->rt_wait_next = NULL;
  return t;
}

static void rt_mutex_park_rt(thread_t* cur) {
  /* 依赖 FULL/RT：定时器可打断 SVC，把 WAITING 当前线程切走 */
  preempt_set_need_resched();
  while (cur->state == THREAD_WAITING) {
    cpu_wait();
  }
  /* 【被停止的等待者不能继续持锁路径】等待期间线程可能被 thread_exit
   * （回收器/异常路径不会等待挂起线程）：若继续走完 rt_mutex_lock 的
   * 竞争循环，僵尸线程会拿到锁后在下一个 tick 再也不会被调度 ——
   * 锁被永久占住（raspi3 上现象：shell 输入几字符后所有 vfs 操作卡死）。
   * 已停止就直接切走，永不返回。 */
  if (cur->state == THREAD_STOPPED) {
    schedule_switch();
  }
}

void rt_mutex_init(rt_mutex_t* m) {
  if (m == NULL) {
    return;
  }
  m->owner = NULL;
  m->depth = 0;
  m->hold_preempt = 0;
  m->owner_priority_saved = 0;
  m->owner_counter_saved = 0;
  m->pi_active = 0;
  m->wait_head = NULL;
}

void rt_mutex_lock(rt_mutex_t* m) {
  thread_t* cur;

  if (m == NULL) {
    return;
  }

  cur = thread_current();

  for (;;) {
    if (m->depth > 0 && m->owner == cur) {
      m->depth++;
      return;
    }

    preempt_disable();
    if (m->owner == NULL) {
      m->owner = cur;
      m->depth = 1;
      m->pi_active = 0;
      /* NONE/VOLUNTARY：持锁期间禁抢占，避免双 SVC 自旋死锁 */
      if (preempt_model < PREEMPT_FULL) {
        m->hold_preempt = 1;
        return;
      }
      m->hold_preempt = 0;
      preempt_enable();
      return;
    }

    rt_mutex_pi_boost(m, cur);

    if (preempt_model >= PREEMPT_RT && cur != NULL) {
      rt_mutex_enqueue(m, cur);
      cur->state = THREAD_WAITING;
      preempt_enable();
      rt_mutex_park_rt(cur);
      continue;
    }

    preempt_enable();

    /* FULL（及无 thread 的早启）：开中断自旋 */
    preempt_set_need_resched();
    while (m->owner != NULL && m->owner != cur) {
    }
  }
}

void rt_mutex_unlock(rt_mutex_t* m) {
  thread_t* wake;
  int held_preempt;

  if (m == NULL) {
    return;
  }

  if (m->depth > 1) {
    m->depth--;
    return;
  }

  held_preempt = m->hold_preempt;
  if (held_preempt == 0) {
    preempt_disable();
  }

  rt_mutex_pi_restore(m);
  m->owner = NULL;
  m->depth = 0;
  m->hold_preempt = 0;

  wake = rt_mutex_dequeue_best(m);
  if (wake != NULL) {
    /* 【不要复活已退出的等待者】等待链上的线程若已 thread_exit/被停止
     * （state != THREAD_WAITING），这里强设 RUNNING 会把一个已死线程
     * 从 STOPPED 状态"复活"——它不在调度链上却继续执行残留上下文，
     * 破坏后续内存/锁状态（raspi3 上表现为输入若干字符后 shell 永久
     * 卡死）。此时只需把它从等待链摘除，锁保持空闲即可。 */
    if (wake->state == THREAD_WAITING) {
      wake->state = THREAD_RUNNING;
      wake->sleep_counter = 0;
    } else {
      wake->rt_wait_next = NULL;
      wake = NULL;
    }
  }

  preempt_enable();

  if (wake != NULL) {
    preempt_set_need_resched();
  }
  if (preempt_model >= PREEMPT_VOLUNTARY) {
    cond_resched();
  }
}
