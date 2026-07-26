/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef KERNEL_RT_MUTEX_H
#define KERNEL_RT_MUTEX_H

#include "thread.h"

/* RT 可重入互斥：
 * - RT：持锁不长期禁抢占；争用者 THREAD_WAITING + cpu_wait，解锁唤醒
 * - FULL：持锁不长期禁抢占；争用开中断自旋
 * - NONE/VOLUNTARY：持锁 preempt_disable，避免 SVC 下互等死锁
 * PI：等待者抬升持有者 priority/counter */
typedef struct rt_mutex {
  thread_t* owner;
  int depth;
  int hold_preempt;
  int owner_priority_saved;
  int owner_counter_saved;
  int pi_active;
  thread_t* wait_head;
} rt_mutex_t;

void rt_mutex_init(rt_mutex_t* m);
void rt_mutex_lock(rt_mutex_t* m);
void rt_mutex_unlock(rt_mutex_t* m);

#endif
