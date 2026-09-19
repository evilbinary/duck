/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef EXCEPTIONS_H
#define EXCEPTIONS_H

#include "arch/arch.h"
#include "platform/platform.h" /* EXCEPTION_NUMBER（每个平台的异常表大小）*/

typedef struct thread thread_t;
typedef void (*fault_hook_fn)(thread_t* t, interrupt_context_t* ic,
                              u64 fault_addr);

/* 异常/中断分发表：下标即 EX_* 值。
 * 【注意】外设中断正在迁移到 duck/kernel/irq.c 的统一 IRQ 框架；
 * 这张表保留给 CPU 异常类以及 tick（EX_IRQ 入口标记 / EX_TIMER）。
 * 迁移完毕前，驱动仍可继续用 exception_regist()。 */
extern interrupt_handler_t exception_handlers[EXCEPTION_NUMBER];
void exception_regist(u32 vec, interrupt_handler_t handler);
void* exception_process(interrupt_context_t* ic);
void interrupt_ack_pending(void);

void exception_init();
void fault_hook_regist(fault_hook_fn fn);


#endif