/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef XTENSA_MM_H
#define XTENSA_MM_H

/* ============================================================================
 * Xtensa（lx6）目标
 * ----------------------------------------------------------------------------
 * 该架构 MMU 语义未在本内核建模，page_map_on 只写 PAGE_P 与 flags，
 * 因此 6 个用途名与 2 个权限位一律为 0（"无附加属性"）。
 * 保留这些名字是为了让共用调用点在所有架构上都能编译，语义为 no-op。
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

/* page_map_on 原型：防止隐式声明导致 flags 丢失（详见 armv7-a/mm.h）。 */
void page_map_on(page_dir_t* l1, u32 virtualaddr, u32 physaddr, u32 flags);

#endif
