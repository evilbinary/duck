/*******************************************************************
* Copyright 2021-2080 evilbinary
* 作者: evilbinary on 01/01/20
* 邮箱: rootdebug@163.com
********************************************************************/
#ifndef SYSCALL_H
#define SYSCALL_H

#include "arch/arch.h"
#include "device.h"
#include "kernel/stdarg.h"
#include "devfn.h"
#include "sysfn.h"
#include "config.h"


typedef void* (*sys_fn_handler_t)(void** syscall_table);

void* syscall0(u32 num);
void* syscall1(u32 num,void* arg0);
void* syscall2(u32 num,void* arg0,void* arg1);
void* syscall3(u32 num,void* arg0,void* arg1,void* arg2);


void syscall_init();
void sys_fn_init_regist(sys_fn_handler_t handler);


#endif