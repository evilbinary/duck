/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef ARM_V7_MM_H
#define ARM_V7_MM_H

/* ============================================================================
 * ARMv7（非 -A）短描述符页表属性
 * ----------------------------------------------------------------------------
 * 与 armv7-a/mm.h 使用同一套 short-descriptor 编码与用途名，保证同一份
 * 调用点代码在两个目标上语义一致。详见 armv7-a/mm.h 的对照表。
 *
 * 【用途名 → 编码(TEX,C,B) → 可缓存】
 *   PAGE_DEV 000,0,1 否 | PAGE_KERNEL 000,1,1 是 | PAGE_KMEM 000,1,1 是
 *   PAGE_USER 000,1,1 是 | PAGE_FB 000,1,1 是 | PAGE_SHARED 001,0,0 否
 * ⚠ 旧写法 001,1,1（L2_TEXT_1|L2_CB）在 TEX 重映射关闭时是 Reserved 编码。
 * ========================================================================== */

/* ---- 描述符字段 ---------------------------------------------------------- */
#define L1_PAGE_TABLE (1 << 0)
#define L2_SMALL_PAGE (2 << 0)

#define L1_PXN (0 << 2)  // The Privileged execute-never bit
#define L1_NS (0 << 3)   // Non-secure bit
#define L1_SBZ (0 << 4)  // Should be Zero

#define L1_DOMAIN(n) (n << 5)

#define L2_XN (0 << 0)    // The Execute-never bit（基值恒 0；置 1 请用 PAGE_XN）
#define L2_CB (3 << 2)    // bit[3:2] = C,B = 0b11
#define L2_NCNB (0 << 2)  // 0b00
#define L2_NCB (1 << 2)   // 0b01 Shareable Device when TEX=0
#define L2_CNB (2 << 2)   // 0b10 Outer/Inner WT, no WA when TEX=0

#define L2_AP_ACCESS (3 << 4)  // AP[1:0]=0b11 full access
#define L2_AP_RW (0 << 9)      // AP[2]=0 read/write
#define L2_AP_RWX (L2_AP_RW | L2_AP_ACCESS)

#define L2_AP_R 0x2
#define L2_TEXT (7 << 6)

#define L2_TEXT_0 (0 << 6)
#define L2_TEXT_1 (1 << 6)
#define L2_TEXT_2 (2 << 6)

#define L2_S (1 << 10)   // The Shareable bit
#define L2_NG (1 << 11)  // The not global bit
#define L2_G (0 << 11)  // The translation is global, meaning the region is available for all processes.

#define L1_DESC (L1_PAGE_TABLE | L1_PXN | L1_NS | L1_SBZ | L1_DOMAIN(0))
#define L2_DESC \
  (L2_XN | L2_SMALL_PAGE | L2_NCNB | L2_AP_ACCESS | L2_AP_RW | L2_S | L2_G)

/* ---- 编码层：4 档合法编码 ------------------------------------------------ */
#define L2_ATTR_WB  (L2_CB)                /* TEX=000 C=1 B=1  Outer/Inner WB       */
#define L2_ATTR_NC  (L2_TEXT_1 | L2_NCNB)  /* TEX=001 C=0 B=0  Normal Non-cacheable */
#define L2_ATTR_DEV (L2_TEXT_0 | L2_NCB)   /* TEX=000 C=0 B=1  Shareable Device     */
#define L2_ATTR_SO  0                      /* TEX=000 C=0 B=0  Strongly-ordered     */

/* ---- 用途层 -------------------------------------------------------------- */
#define PAGE_P 0 /* 无附加属性（L2_DESC 基值已含权限/类型位） */
#define PAGE_R 0

#ifndef MM_KMEM_CACHEABLE
#define MM_KMEM_CACHEABLE 1 /* 0 ⇒ 内核堆退回非缓存（应急二分定位用） */
#endif

#define PAGE_DEV     L2_ATTR_DEV  /* 设备 MMIO                                */
#define PAGE_KERNEL  L2_ATTR_WB   /* 内核代码/映像/只读数据                    */
#define PAGE_USER    L2_ATTR_WB   /* 用户内存                                  */
#define PAGE_FB      L2_ATTR_WB   /* 帧缓冲及其别名                            */
#define PAGE_SHARED  L2_ATTR_NC   /* CPU 与硬件共享缓冲                        */
#if MM_KMEM_CACHEABLE
#define PAGE_KMEM L2_ATTR_WB /* 内核堆 */
#else
#define PAGE_KMEM L2_ATTR_NC
#endif

/* ---- 权限层：与用途名正交，按需 OR；默认不启用（零行为变更） -------------
 * PAGE_RO: AP[2]=1 ⇒ 只读；PAGE_XN: bit[0]=1 ⇒ 0b10→0b11 不可执行。
 * ⚠ 置 PAGE_XN 会切换为「扩展格式」，须复核 TEX/C/B 编码表。 */
#define PAGE_RO (1u << 9)
#define PAGE_XN (1u << 0)

typedef u32 page_dir_t;

/* page_map_on 原型：防止调用方走隐式声明导致 flags 参数丢失（详见
 * armv7-a/mm.h 的说明）。签名必须与 mm.c 定义一致。 */
void page_map_on(page_dir_t* l1, u32 virtualaddr, u32 physaddr, u32 flags);

#endif
