/*******************************************************************
* Copyright 2021-present evilbinary
* 作者: evilbinary on 01/01/20
* 邮箱: rootdebug@163.com
********************************************************************/
#ifndef SCHEDULE_H
#define SCHEDULE_H

#include "arch/arch.h"
#include "thread.h"

void schedule_init(void);
u32 schedule_get_ticks(void);
u32 schedule_get_ticks_cpu(int cpu);
void schedule(interrupt_context_t* ic);
void schedule_switch(void);
void schedule_sleep(u32 nsec);
void* do_schedule(interrupt_context_t* ic);

/* 在已判定可调度时执行切换，返回应恢复的 ic */
interrupt_context_t* schedule_reschedule(interrupt_context_t* ic);

#endif
