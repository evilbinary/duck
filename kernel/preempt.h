/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef KERNEL_PREEMPT_H
#define KERNEL_PREEMPT_H

#include "arch/arch.h"
#include "libs/include/types.h"

/* 全局抢占模型（系统级，非 per-task） */
enum {
  PREEMPT_NONE = 0,      /* 仅回用户态前可调度 */
  PREEMPT_VOLUNTARY = 1, /* + cond_resched 显式点 */
  PREEMPT_FULL = 2,      /* 内核态也可被时钟抢走（preempt_count==0） */
  PREEMPT_RT = 3, /* FULL + rt_mutex 睡眠等待/PI，持锁不长期禁抢占 */
};

#ifndef PREEMPT_MODEL_DEFAULT
#define PREEMPT_MODEL_DEFAULT PREEMPT_NONE
#endif

extern int preempt_model;

void preempt_init(void);
void preempt_set_model(int model);
int preempt_get_model(void);

void preempt_disable(void);
void preempt_enable(void);
int preempt_count_get(void);

void preempt_set_need_resched(void);
void preempt_clear_need_resched(void);
int preempt_need_resched(void);

/* 定时器/IRQ 路径：此刻能否 context_switch */
int preempt_may_switch(interrupt_context_t* ic);

/* 回用户态前（所有模式）：若 need_resched 则切换，返回应恢复的 ic */
interrupt_context_t* preempt_on_return_user(interrupt_context_t* ic);

/* VOLUNTARY+：显式点；当前仅置 need_resched，真正切换在 IRQ/syscall 出口 */
void cond_resched(void);

/* FULL：解锁等路径；有合法 ic 时才尝试（见实现注释） */
void preempt_check_resched(void);

#endif
