/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef ARM_V5_MM_H
#define ARM_V5_MM_H

/* ============================================================================
 * ARMv5（VMSAv5）短描述符页表属性
 * ----------------------------------------------------------------------------
 * 【用途名 → 编码(TEX,C,B) → 可缓存】
 *   PAGE_DEV 000,0,1 否 | PAGE_KERNEL 000,1,1 是 | PAGE_KMEM 000,1,1 是
 *   PAGE_USER 000,1,1 是 | PAGE_FB 000,1,1 是 | PAGE_SHARED 001,0,0 否
 * 说明：ARMv5 无 TEX 位（bit[8:6] 为 SBZ），旧写法 L2_TEXT_1|L2_CB 与
 *       L2_CB 在硬件上等价，这里统一为不含 bit6 的纯 C/B 编码。
 *       ARMv5 亦无 XN，故 PAGE_XN 留作占位。
 * ========================================================================== */

#define L1_PAGE_TABLE (1 << 0) | (1 << 4)
#define L2_SMALL_PAGE (2 << 0)

#define L1_PXN (0 << 2)  // The Privileged execute-never bit
#define L1_NS (0 << 3)   // Non-secure bit
#define L1_SBZ (0 << 4)  // Should be Zero

#define L1_DOMAIN(n) (n << 5)

#define L2_XN (0 << 0)
#define L2_CB (3 << 2)    // bit[3:2] = C,B = 0b11
#define L2_NCNB (0 << 2)  // 0b00
#define L2_NCB (1 << 2)   // 0b01
#define L2_CNB (2 << 2)   // 0b10

#define L2_AP_RW_ALL (3 << 4)   // full access（用户可写）
#define L2_AP_RW_PRIV (1 << 4)  // read write privilege level
#define L2_AP_RWX L2_AP_RW_ALL

/* AP[1:0]=0b10：特权可写、用户只读 —— COW 共享页用它做写保护 */
#define L2_AP_RO (2 << 4)

/* 软件位（ARMv5 小页描述符 bit[9] 为 IMP，可作私用）：
 * 标记该页是 fork 后共享的 COW 页 */
#define L2_COW (1 << 9)
#define L2_TEXT (7 << 6)

#define L2_TEXT_0 (0 << 6)
#define L2_TEXT_1 (1 << 6)
#define L2_TEXT_2 (2 << 6)

#define L2_S (1 << 10)   // The Shareable bit
#define L2_NG (1 << 11)  // The not global bit
#define L2_G (0 << 11)   // 全局

#define L1_DESC (L1_PAGE_TABLE | L1_SBZ | L1_DOMAIN(0))
#define L2_DESC (L2_SMALL_PAGE | L2_NCNB | L2_AP_RW_ALL)

/* ---- 编码层：4 档合法编码（L2_DESC 基值已含全权限位，此处只管缓存属性） -- */
#define L2_ATTR_WB  (L2_CB)                /* C=1 B=1  可缓存（写回）              */
#define L2_ATTR_WT  (L2_CNB)               /* C=1 B=0  可缓存（写通，=2<<2）        */
#define L2_ATTR_NC  (L2_TEXT_1 | L2_NCNB)  /* C=0 B=0  非缓存（bit6 在 ARMv5 为 SBZ） */
#define L2_ATTR_DEV (L2_TEXT_0 | L2_NCB)   /* C=0 B=1  Device                      */
#define L2_ATTR_SO  0                      /* C=0 B=0  强序                        */

/* ---- 用途层 -------------------------------------------------------------- */
#define PAGE_P 0
#define PAGE_R 0

#define PAGE_DEV     L2_ATTR_DEV  /* 设备 MMIO                     */
/* 可缓存映射保持写回(WB)，与 Linux 对 ARM926 的用法一致
 * （proc-arm926.S 的 __arm926_proc_info = BUFFERABLE|CACHEABLE）。
 * 切地址空间时的整片清理由 arch/armv5/context.c + cpu.c 的
 * test,clean,invalidate（MRC c7,c14,3）完成，见 ARMV5_DCACHE 的说明。 */
#define PAGE_KERNEL  L2_ATTR_WB   /* 内核代码/映像/只读数据         */
#define PAGE_KMEM    L2_ATTR_WB   /* 内核堆                         */
#define PAGE_USER    L2_ATTR_WB   /* 用户内存                       */
#define PAGE_FB      L2_ATTR_WB   /* 帧缓冲及其别名                 */
#define PAGE_SHARED  L2_ATTR_NC   /* CPU 与硬件共享缓冲             */

/* ---- 权限层：本架构无法以"纯 OR"表达，故为占位 0（默认零行为变更） -------
 * 接入方式（将来需要时）：
 *   PAGE_RO：在 page_map_on 内改 AP 组合（ARMv5 无全局只读位，只有
 *            AP[1:0]=0b10 的"特权可写/用户只读"语义）；
 *   PAGE_XN：ARMv5 无 XN 位，硬件层面恒可执行。 */
#define PAGE_RO 0
#define PAGE_XN 0

typedef u32 page_dir_t;

/* page_map_on 原型：防止隐式声明导致 flags 丢失（详见 armv7-a/mm.h）。 */
void page_map_on(page_dir_t* l1, u32 virtualaddr, u32 physaddr, u32 flags);

/* ---- COW 纯机制（不含策略/引用计数；引用计数在 kernel 侧） ----
 * page_put_page：释放用户页时由 arch 回调，交给 kernel 决定"是否真的 free"。
 * page_cow_query：查该 VA 是否 COW 页，并带出物理页。
 * page_cow_apply：new_pa==0 标 COW（只读+标记）；!=0 破写指向 new_pa 可写。
 * page_fault_is_write 在 cpu.c。 */
extern int (*page_put_page)(void* pa);
int page_cow_query(u32* upage, u32 va, u32* pa);
void page_cow_apply(u32* upage, u32 va, u32 new_pa);
int page_fault_is_write(void);

#endif
