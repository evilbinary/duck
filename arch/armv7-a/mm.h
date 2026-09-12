/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef ARM_V7A_MM_H
#define ARM_V7A_MM_H

/* ============================================================================
 * ARMv7-A 短描述符（small page）页表属性
 * ----------------------------------------------------------------------------
 * 本文件是 armv7-a 目标【唯一】一处「用途 → 编码」的定义处。
 * 调用点（kernel/、arch/、platform/、modules/）只使用下面 6 个用途名，
 * 不再自己拼 TEX/C/B 位。
 *
 * 【用途名 → 编码 → 是否可缓存 → 适用对象】
 *   用途名         编码(TEX,C,B)  可缓存  适用对象
 *   PAGE_DEV       000,0,1        否      UART/GIC/CCU/MMC/DMA 等设备 MMIO
 *   PAGE_KERNEL    000,1,1        是      内核代码、内核映像、只读数据
 *   PAGE_KMEM      000,1,1        是      内核堆（kmalloc / ya_sbrk）
 *   PAGE_USER      000,1,1        是      用户内存（程序/堆/栈）
 *   PAGE_FB        000,1,1        是      帧缓冲及其高位别名
 *   PAGE_SHARED    001,0,0        否      CPU 与硬件共享缓冲（读盘/DMA 描述符）
 *
 * 【编码前提】本内核 SCTLR.TRE = 0（TEX 重映射关闭，实机 sctlr=0xc5187d 已确认）。
 * 此时 short-descriptor 的 (TEX,C,B) 只有以下 4 档是「合法且含义明确」的：
 *     000,0,0 = Strongly-ordered（不进 cache、强序）
 *     000,0,1 = Shareable Device
 *     000,1,1 = Outer/Inner Write-Back（可缓存，本内核性能来源）
 *     001,0,0 = Normal Non-cacheable
 * ⚠ 001,1,1（旧写法 L2_TEXT_1|L2_CB = 0x4C）在 TRE=0 下是 Reserved 编码，
 *   实机表现为「完全不进 cache」（内核内存 238~271 周期/字节、渲染 7fps、
 *   blit≈127ms/帧），已彻底停用，见 L2_ATTR_* 注释。
 * ========================================================================== */

/* ---- 描述符字段 ---------------------------------------------------------- */
#define L1_PAGE_TABLE (1 << 0)
#define L2_SMALL_PAGE (2 << 0) /* bit[1:0]=0b10 ⇒ XN=0（ARMv6 兼容格式） */

#define L1_PXN (0 << 2)  // The Privileged execute-never bit
#define L1_NS (0 << 3)   // Non-secure bit
#define L1_SBZ (0 << 4)  // Should be Zero

#define L1_DOMAIN(n) (n << 5)

#define L2_XN (0 << 0)    // The Execute-never bit（基值恒 0；置 1 请用 PAGE_XN）
#define L2_CB (3 << 2)    // bit[3:2] = C,B = 0b11
#define L2_NCNB (0 << 2)  // 0b00
#define L2_NCB (1 << 2)   // 0b01
#define L2_CNB (2 << 2)   // 0b10

#define L2_AP_ACCESS (3 << 4)  // AP[1:0]=0b11 full access
#define L2_AP_RW (0 << 9)      // AP[2]=0 read/write any privilege level
#define L2_AP_RWX (L2_AP_RW | L2_AP_ACCESS)

#define L2_AP_R 0x2
#define L2_TEXT (7 << 6)

#define L2_TEXT_0 (0 << 6)  // TEX[0]=0
#define L2_TEXT_1 (1 << 6)  // TEX[0]=1
#define L2_TEXT_2 (2 << 6)  // TEX[1]=1

#define L2_S (1 << 10)   // The Shareable bit
#define L2_NG (1 << 11)  // The not global bit
#define L2_G (0 << 11)   // 全局（对所有进程可见）

#define L1_DESC (L1_PAGE_TABLE | L1_PXN | L1_NS | L1_SBZ | L1_DOMAIN(0))
#define L2_DESC \
  (L2_XN | L2_SMALL_PAGE | L2_NCNB | L2_AP_ACCESS | L2_AP_RW | L2_S | L2_G)

/* ---- 编码层：4 档合法编码（唯一入口，勿在别处直拼 TEX/C/B） -------------- */
#define L2_ATTR_WB  (L2_CB)                /* TEX=000 C=1 B=1  Outer/Inner WB       */
#define L2_ATTR_NC  (L2_TEXT_1 | L2_NCNB)  /* TEX=001 C=0 B=0  Normal Non-cacheable */
#define L2_ATTR_DEV (L2_TEXT_0 | L2_NCB)   /* TEX=000 C=0 B=1  Shareable Device     */
#define L2_ATTR_SO  0                      /* TEX=000 C=0 B=0  Strongly-ordered     */

/* ---- 用途层：调用点只用这 6 个名字 -------------------------------------- */
#define PAGE_P 0  /* 无附加属性（L2_DESC 基值已含权限/类型位） */
#define PAGE_R 0

/* 内核堆是否可缓存：默认 1（性能）。
 * 置 0 ⇒ PAGE_KMEM 退回 Normal Non-cacheable，只影响内核堆，
 * 用于冷启动二分定位「CPU 与硬件共享缓冲」的一致性缺口。 */
#ifndef MM_KMEM_CACHEABLE
#define MM_KMEM_CACHEABLE 1
#endif

#define PAGE_DEV     L2_ATTR_DEV  /* 设备 MMIO                                */
#define PAGE_KERNEL  L2_ATTR_WB   /* 内核代码/映像/只读数据                    */
#define PAGE_USER    L2_ATTR_WB   /* 用户内存                                  */
#define PAGE_FB      L2_ATTR_WB   /* 帧缓冲及其别名（须与 gpu/xwin 侧同属性）  */
#define PAGE_SHARED  L2_ATTR_NC   /* CPU 与硬件共享缓冲（读盘缓冲/DMA 描述符） */
#if MM_KMEM_CACHEABLE
#define PAGE_KMEM L2_ATTR_WB /* 内核堆 */
#else
#define PAGE_KMEM L2_ATTR_NC
#endif

/* ---- 权限层：与用途名正交，按需 OR；本期默认不启用（零行为变更） ---------
 * PAGE_RO: AP[2]=1 ⇒ 只读（PL0/PL1 均不可写）。基值 AP[2]=0，故可纯 OR 生效。
 * PAGE_XN: bit[0]=1 ⇒ 0b10 变 0b11，该页不可执行。
 *   ⚠ 置 PAGE_XN 后描述符进入「扩展格式」，TEX[2:0]/C/B 的编码表与
 *     ARMv6 兼容格式（0b10）不同 ⇒ 启用前必须按 ARM ARM 复核该页缓存
 *     编码未因格式切换而改变。
 * 候选用途（将来按需追加，本期不改任何调用点）：
 *   PAGE_XN：PAGE_DEV / PAGE_SHARED / PAGE_KMEM / PAGE_FB
 *   PAGE_RO：内核只读段（.rodata） */
#define PAGE_RO (1u << 9)
#define PAGE_XN (1u << 0)

/* 【关键】page_map_on 必须在此声明！
 * 否则其它编译单元（duck/arch/pmemory.c、duck/kernel/page.c、memory.c、vma.c）
 * 会走"隐式声明"，参数按无原型规则传递：调用方把 u64 的 flags 放进 r2:r3，
 * 而被调用方（u32 flags）只读 r3 = 高 32 位 = 0 ⇒ 无论传 0xC 还是 0x4C，
 * 入口看到的永远是 0 ⇒ 所有页被映成 TEX=000,C=0,B=0 = Strongly-ordered。
 * 实机 t113 铁证：调用方内联打印 flags=c，但描述符 l2e=0x401bd432、
 * 内核内存 238~271 周期/字节、blit≈130ms/帧、整机 7fps（QEMU 忽略属性）。
 * 注意：签名必须与 mm.c 的定义完全一致（u32* 即 page_dir_t*）。 */
void page_map_on(u32* l1, u32 virtualaddr, u32 physaddr, u32 flags);

typedef u32 page_dir_t;

#endif
