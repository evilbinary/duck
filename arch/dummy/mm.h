/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef DUMMY_MM_H
#define DUMMY_MM_H

/* ============================================================================
 * dummy 目标（无 MMU / 直接映射）
 * ----------------------------------------------------------------------------
 * 本架构没有页表属性可言，page_map_on 是空实现，因此 6 个用途名与 2 个
 * 权限位全部为 0（"无附加属性"）。保留这些名字是为了让共用调用点在
 * 所有架构上都能编译，语义为 no-op。
 * ========================================================================== */

#define PAGE_P 0
#define PAGE_R 0

/* 用途层：no-op */
#define PAGE_DEV 0     /* 设备 MMIO        */
#define PAGE_KERNEL 0  /* 内核代码/映像    */
#define PAGE_KMEM 0    /* 内核堆           */
#define PAGE_USER 0    /* 用户内存         */
#define PAGE_FB 0      /* 帧缓冲           */
#define PAGE_SHARED 0  /* 硬件共享缓冲     */

/* 权限层：no-op */
#define PAGE_RO 0
#define PAGE_XN 0

typedef u32 page_dir_t;

/* page_map_on 原型：防止隐式声明导致 flags 丢失（详见 armv7-a/mm.h）。
 * page_map 的声明在 duck/kernel/page.h，此处不重复。 */
void page_map_on(page_dir_t* l1, u32 virtualaddr, u32 physaddr, u32 flags);

#endif
