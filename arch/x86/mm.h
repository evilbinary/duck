/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef X86_MM_H
#define X86_MM_H

/* ============================================================================
 * x86（分页）页表属性
 * ----------------------------------------------------------------------------
 * 分页项只有 P(存在)/RW(可写)/US(用户) 三个权限位，没有缓存属性位
 * （缓存由 PAT/MTRR 决定），因此 6 个用途名只表达"权限组合"。
 *
 * 【用途名 → 分页位】
 *   全部 6 个用途名 = P | RW | US（沿用改造前的取值，零行为变更）
 * ========================================================================== */

#define PAGE_P 1  /* bit0 Present */
#define PAGE_R 0

#define X86_PTE_W 2  /* bit1 Read/Write */
#define X86_PTE_US 4 /* bit2 User/Supervisor */

/* ---- 用途层（沿用改造前的取值，保证零行为变更） ------------------------- */
#define PAGE_DEV (PAGE_P | X86_PTE_W | X86_PTE_US)     /* 设备 MMIO          */
#define PAGE_KERNEL (PAGE_P | X86_PTE_W | X86_PTE_US)  /* 内核代码/映像      */
#define PAGE_KMEM (PAGE_P | X86_PTE_W | X86_PTE_US)    /* 内核堆             */
#define PAGE_USER (PAGE_P | X86_PTE_W | X86_PTE_US)    /* 用户内存           */
#define PAGE_FB (PAGE_P | X86_PTE_W | X86_PTE_US)      /* 帧缓冲及其别名     */
#define PAGE_SHARED (PAGE_P | X86_PTE_W | X86_PTE_US)  /* CPU 与硬件共享缓冲 */

/* ---- 权限层：基值已含写位、且 32 位非 PAE 无 NX，无法以"纯 OR"表达 ------
 * 接入方式（将来需要时）在 page_map_on 的合成处清位：
 *   PAGE_RO：清 X86_PTE_W；PAGE_XN：需 PAE + EFER.NXE，本内核未启用。 */
#define PAGE_RO 0
#define PAGE_XN 0

typedef u64* page_dir_t;

/* page_map_on 原型：防止隐式声明导致 flags 丢失（详见 armv7-a/mm.h）。
 * 注意：本架构 page_dir_t 是 u64*，故参数为 page_dir_t*（= u64**），
 * 与 mm.c 内定义完全一致。 */
void page_map_on(page_dir_t* page, u32 virtualaddr, u32 physaddr, u32 flags);

void* page_v2p(u64* page_dir_ptr_tab, void* vaddr);

#endif
