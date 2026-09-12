/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef GENERAL_MM_H
#define GENERAL_MM_H

/* ============================================================================
 * general 目标：无 MMU 架构的兜底头
 * ----------------------------------------------------------------------------
 * 语义为"无附加属性"：6 个用途名与 2 个权限位一律为 0。
 * 注意：本文件不再承担"把别处的属性纠正/覆盖"的职责 —— 每个架构都在
 * 自己的 mm.h 里原生定义用途名，属性来源唯一。
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
