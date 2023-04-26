/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef RISCV_MM_H
#define RISCV_MM_H

#define PTE_V     0x001   // Valid
#define PTE_R     0x002   // Readable
#define PTE_W     0x004   // Writable
#define PTE_X     0x008   // Executable
#define PTE_U     0x010   // User accessible
#define PTE_G     0x020   // Global
#define PTE_A     0x040   // Accessed
#define PTE_D     0x080   // Dirty

#define PAGE_P   0
#define PAGE_R  0
#define PAGE_X  0
#define PAGE_RWR   0 //读执行
#define PAGE_RWW   0 //读/写/执行
#define PAGE_USS   0 //系统级
#define PAGE_USU   0 //用户级
#define PAGE_DEV   0 //设备级
#define PAGE_RWE   0
#define PAGE_RW    0

typedef u32 page_dir_t;





#endif