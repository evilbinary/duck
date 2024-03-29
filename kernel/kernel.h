/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef KERNEL_H
#define KERNEL_H

#include "config.h"
#include "arch/arch.h"
#include "kernel/device.h"
#include "kernel/memory.h"
#include "kernel/module.h"
#include "kernel/syscall.h"
#include "kernel/thread.h"
#include "kernel/exceptions.h"
#include "kernel/stdarg.h"
#include "kernel/ioctl.h"
#include "kernel/vfs.h"
#include "kernel/stat.h"
#include "kernel/logger.h"
#include "kernel/event.h"
#include "kernel/loader.h"
#include "kernel/devfn.h"
#include "algorithm/circle_queue.h"
#include "algorithm/queue_pool.h"

void kernel_init();
void kernel_run();

#endif