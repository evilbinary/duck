/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef PAGE_H
#define PAGE_H

#include "arch/arch.h"
#include "thread.h"

void page_fault_handle(interrupt_context_t *context);

#endif