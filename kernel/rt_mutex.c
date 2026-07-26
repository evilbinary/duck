/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "rt_mutex.h"

#include "preempt.h"

void rt_mutex_init(rt_mutex_t* m) {
  if (m == NULL) {
    return;
  }
  m->owner = NULL;
  m->depth = 0;
  m->owner_counter_saved = 0;
  m->pi_active = 0;
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
      preempt_enable();
      return;
    }

    /* 简易 PI：压低持有者 counter，使其更快被 schedule_next 选中 */
    if (cur != NULL && m->owner != NULL && m->pi_active == 0 &&
        cur->counter < m->owner->counter) {
      m->owner_counter_saved = m->owner->counter;
      m->owner->counter = cur->counter;
      m->pi_active = 1;
    }
    preempt_enable();

    /* 争用：开中断自旋，FULL/RT 下时钟可跑持有者 */
    preempt_set_need_resched();
    while (m->owner != NULL && m->owner != cur) {
    }
  }
}

void rt_mutex_unlock(rt_mutex_t* m) {
  if (m == NULL) {
    return;
  }

  if (m->depth > 1) {
    m->depth--;
    return;
  }

  preempt_disable();
  if (m->pi_active != 0 && m->owner != NULL) {
    m->owner->counter = m->owner_counter_saved;
    m->pi_active = 0;
  }
  m->owner = NULL;
  m->depth = 0;
  preempt_enable();

  if (preempt_model >= PREEMPT_VOLUNTARY) {
    cond_resched();
  }
}
