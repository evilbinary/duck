/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef PAGE_H
#define PAGE_H

#include "arch/arch.h"
#include "thread.h"
#include "exceptions.h"
#include "schedule.h"
#include "memory.h"

void* page_fault_handle(interrupt_context_t *context);
void page_map(vaddr_t virtualaddr, vaddr_t physaddr, u32 flags);
void page_map_current(vaddr_t virtualaddr, vaddr_t physaddr, u32 flags);
void page_init(void);

// Forward declarations for functions used in page fault handling
void schedule(interrupt_context_t* ic);
void* extend_stack(void* addr, size_t size);
void exception_regist(u32 vec, interrupt_handler_t handler);

#endif