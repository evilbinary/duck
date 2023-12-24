/*******************************************************************
* Copyright 2021-present evilbinary
* 作者: evilbinary on 01/01/20
* 邮箱: rootdebug@163.com
********************************************************************/
#ifndef TARCE_H
#define TRACE_H

#include "kernel/kernel.h"

typedef struct ytrace{
    sys_fn_handler_t origin_call;
    int cmd[64];

    int counts[SYSCALL_NUMBER];
    int times[SYSCALL_NUMBER];
    int total_count;//所有调用次数
}ytrace_t;

#endif