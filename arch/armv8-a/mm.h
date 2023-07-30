/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef ARM_MM_H
#define ARM_MM_H




#define L2_XN (0 << 0)  // The Execute-never bit
#define L2_CB (3 << 2)  // 0b11 cache write-back
#define L2_NCNB (0 << 2) // 0b00 Non-cacheable
#define L2_NCB (1 << 2) // 0b01 Write-Back, Write-Allocate
#define L2_CNB (0 << 2) // 0b10 Write-Through, no Write-Allocate


#define L2_TEXT_0 (0 << 6)
#define L2_TEXT_1 (1 << 6)
#define L2_TEXT_2 (2 << 6)

#define PAGE_P   0
#define PAGE_R  0
#define PAGE_RX   (L2_TEXT_1 | L2_CB) //读执行
#define PAGE_RW   (L2_CB)
#define PAGE_RWX  (L2_TEXT_1 | L2_CB) //读/写/执行
#define PAGE_RW_NC   (L2_NCB)

#define PAGE_P   0
#define PAGE_R  0
#define PAGE_RX   (L2_TEXT_1 | L2_CB) //读执行
#define PAGE_RW   (L2_CB)
#define PAGE_RWX  (L2_TEXT_1 | L2_CB) //读/写/执行
#define PAGE_RW_NC   (L2_NCB)


#define PAGE_SYS   (L2_TEXT_1|L2_CB) //系统级
#define PAGE_USR   (L2_TEXT_1|L2_CB) //用户级
#define PAGE_DEV   (L2_TEXT_0|L2_NCNB) //设备级

typedef u32 page_dir_t;

#endif