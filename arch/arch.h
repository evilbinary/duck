/*******************************************************************
* Copyright 2021-present evilbinary
* 作者: evilbinary on 01/01/20
* 邮箱: rootdebug@163.com
********************************************************************/
#ifndef ARCH_H
#define ARCH_H

#include "libs/include/types.h"
#include "boot.h"
#include "cpu.h"
#include "display.h"
#include "pmemory.h"
#include "libs/include/kernel/common.h"
#include "libs/include/kernel/elf.h"
#include "interrupt.h"
#include "lock.h"
#include "platform/platform.h"

void arch_init(boot_info_t* boot,int cpu_id);

ullong mm_get_total(void);
/* 物理 RAM 基址（遍历内存块 origin_addr，跨平台通用） */
ullong mm_get_base(void);

#endif