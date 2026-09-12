/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef RISCV_MM_H
#define RISCV_MM_H

/* ============================================================================
 * RISC-V（Sv32）页表属性
 * ----------------------------------------------------------------------------
 * Sv32 的 PTE 只有 R/W/X/U 权限位，没有缓存属性位（缓存由 PMA/PMP 决定），
 * 因此 6 个用途名只表达"权限组合"，缓存语义在本架构上缺省。
 *
 * 【用途名 → PTE 组合】
 *   PAGE_DEV / PAGE_KERNEL / PAGE_KMEM / PAGE_FB / PAGE_SHARED = R|W|X
 *   PAGE_USER                                                  = R|W|X|U
 * ========================================================================== */

#define PTE_V 0x001  // Valid
#define PTE_R 0x002  // Readable
#define PTE_W 0x004  // Writable
#define PTE_X 0x008  // Executable
#define PTE_U 0x010  // User accessible
#define PTE_G 0x020  // Global
#define PTE_A 0x040  // Accessed
#define PTE_D 0x080  // Dirty

#define PAGE_P PTE_V
#define PAGE_R PTE_R
#define PAGE_X PTE_X

/* ---- 用途层（沿用改造前的取值，保证零行为变更） ------------------------- */
#define PAGE_DEV (PTE_R | PTE_W | PTE_X)          /* 设备 MMIO                */
#define PAGE_KERNEL (PTE_R | PTE_W | PTE_X)       /* 内核代码/映像            */
#define PAGE_KMEM (PTE_R | PTE_W | PTE_X)         /* 内核堆                   */
#define PAGE_USER (PTE_R | PTE_W | PTE_X | PTE_U) /* 用户内存                 */
#define PAGE_FB (PTE_R | PTE_W | PTE_X)           /* 帧缓冲及其别名           */
#define PAGE_SHARED (PTE_R | PTE_W | PTE_X)       /* CPU 与硬件共享缓冲       */

/* ---- 权限层：Sv32 基值已含 W/X，无法以"纯 OR"取反，故为占位 0 ----------
 * 接入方式（将来需要时）在 page_map_on 的合成处清位：
 *   PAGE_RO：清 PTE_W；PAGE_XN：清 PTE_X。 */
#define PAGE_RO 0
#define PAGE_XN 0

typedef u32 page_dir_t;

/* page_map_on 原型：防止隐式声明导致 flags 丢失（详见 armv7-a/mm.h）。 */
void page_map_on(page_dir_t* l1, u32 virtualaddr, u32 physaddr, u32 flags);

#endif
