/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef ARM_MM_H
#define ARM_MM_H


#define L1_PAGE_TABLE (1 << 0)
#define L2_SMALL_PAGE (2 << 0)

#define L1_PXN (0 << 2)  // The Privileged execute-never bit
#define L1_NS (0 << 3)   // Non-secure bit
#define L1_SBZ (0 << 4)  // Should be Zero

#define L1_DOMAIN(n) (n << 5)

#define L2_XN (0 << 0)  // The Execute-never bit
/* Short-descriptor TEX/C/B (TRE=0): CB alone is not enough for Normal NC */
#define L2_CB (3 << 2)    // 0b11 Outer/Inner WB, no WA (with TEX=0)
#define L2_NCNB (0 << 2)  // 0b00
#define L2_NCB (1 << 2)   // 0b01 Shareable Device when TEX=0
#define L2_CNB (2 << 2)   // 0b10 Outer/Inner WT, no WA when TEX=0

#define L2_AP_ACCESS (3 << 4) //full access
#define L2_AP_RW (0 << 9)  // read write any privilege level
#define L2_AP_RWX (L2_AP_RW|L2_AP_ACCESS)

#define L2_AP_R  0x2
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

#define PAGE_P   0
#define PAGE_R  0
/* 【修复】ARMv7 短描述符 + TEX remap 关闭（本内核 SCTLR.TRE=0，实机寄存器
 * 已确认）时，只有 TEX=000 配 C/B 才是"正常可缓存"的编码：
 *     TEX=000,C=1,B=1 = Outer/Inner Write-Back, no Write-Allocate  ← 用这个
 *     TEX=001,C=1,B=1 = Reserved（下面的原注释误以为它是 Normal WB）
 * 原值 L2_TEXT_1|L2_CB = 0x4C 正是后者 ⇒ 实机完全不走 cache。
 * 证据：内核堆页描述符 l2e=0x401bd432、内核内存实测 238~271 周期/字节、
 * blit≈130ms/帧、整机 7fps；而 QEMU 忽略内存属性 ⇒ 只有实机慢。
 * 去掉 L2_TEXT_1 只改缓存属性（bit0=XN、bit[5:4]权限位不动）⇒ 不影响
 * 可执行性与权限。 */
#define PAGE_RX   (L2_CB) //读执行
#define PAGE_RW   (L2_CB)
#define PAGE_RWX  (L2_CB) //读/写/执行
/* Normal Non-cacheable: TEX=001, C=B=0（勿用 TEX=0+CB=01，那是 Device）
 * 注意：这条是 "001 编码"的正当用法（Non-cacheable），保留 L2_TEXT_1。 */
#define PAGE_RW_NC (L2_TEXT_1 | L2_NCNB)


#define PAGE_SYS   (L2_CB) //系统级
#define PAGE_USR   (L2_CB) //用户级
#define PAGE_DEV   (L2_TEXT_0|L2_NCB) //设备级（Shareable Device）

/* ============ 临时二分开关（定位完请删掉） ============
 * 打开下面这行 ⇒ 内核/用户 RAM 强制映射为"Normal Non-cacheable"，
 * 等价于修复前的旧行为（那时 page_map_on 缺原型，flags 恒为 0 = Strongly-ordered，
 * 一切都"碰巧"是 CPU 与硬件一致的）。
 *
 * 用途：判断现在卡在 "kernel run start" 之后、没有任何输出，是否由
 *       "内存真正可缓存" 引起 —— 也就是 SD/MMC DMA、LCD DE 这类
 *       CPU 与硬件共享的缓冲缺 cache 维护。
 *
 * 用法：
 *   打开 ⇒ 能进 shell（但会变慢）  ⇒ 确认是"可缓存化"引起，下一步给共享缓冲
 *                                    加 cache 维护/非缓存映射，然后关掉本开关；
 *   打开 ⇒ 仍然卡死              ⇒ 与缓存无关，方向转向从核/中断/调度。
 * 关闭（默认）即为性能修复后的状态。 */
/* 【已关闭】MMC/FatFs 共享缓冲的 cache 维护已补（见 sunxi-sdhci.c 的
 * mmc_read_blocks/mmc_write_blocks），现在内核与用户内存都走可缓存，拿回性能。
 * 若再次出现"启动后无输出/卡死"，把下面这行取消注释即可回到非缓存
 * （稳但不快，用于快速恢复与二分）。 */
// #define MM_RAM_NO_CACHE 1
#ifdef MM_RAM_NO_CACHE
#undef PAGE_RWX
#undef PAGE_USR
#define PAGE_RWX (L2_TEXT_1 | L2_NCNB) /* TEX=001,C=0,B=0 = Normal Non-cacheable */
#define PAGE_USR (L2_TEXT_1 | L2_NCNB)
#endif

/* 【修复·关键】page_map_on 必须在此声明！
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