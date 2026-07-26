/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef KERNEL_RT_MUTEX_H
#define KERNEL_RT_MUTEX_H

#include "thread.h"

/* 可重入互斥：持锁期间不长期 preempt_disable，争用时开中断自旋并做简易 PI。
 * RT 模式下替代“spin + 整段禁抢占”；真正 block+返回需后续补全调度路径。 */
typedef struct rt_mutex {
  thread_t* owner;
  int depth;
  int owner_counter_saved;
  int pi_active;
} rt_mutex_t;

void rt_mutex_init(rt_mutex_t* m);
void rt_mutex_lock(rt_mutex_t* m);
void rt_mutex_unlock(rt_mutex_t* m);

#endif
