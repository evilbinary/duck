/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
/* 物理页引用计数（kernel 层旁路表）。
 *
 * COW 是主要使用者：用户页分配时 ref=1；fork 共享可写页时 ref++；
 * 破写/exec/exit 释放时 ref--，归零才真正 mm_free_page。
 *
 * 【分层】本表归 kernel；arch 的 mm.c 不直接 include/调用这里，而是通过
 * arch 自己的 page_put_page hook 回调（见各 arch 的 mm.h），由 kernel 注册。
 */
#ifndef KERNEL_PAGE_REF_H
#define KERNEL_PAGE_REF_H

#include "kernel/kernel.h"


void page_ref_init(void);
u32 page_ref_get(u32 pa);
void page_ref_set(u32 pa, u32 v);
void page_ref_inc(u32 pa);
/* 减一，返回减后的引用数（0 = 应当释放） */
u32 page_ref_dec(u32 pa);

/* 供 arch 的 page_put_page hook 用：返回 0 = 允许释放 */
int page_put(void* pa);

#endif
