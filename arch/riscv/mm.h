/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef RISCV_MM_H
#define RISCV_MM_H

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
#define PAGE_RX (PTE_R | PTE_X)          // 读执行
#define PAGE_RW  PTE_W
#define PAGE_RWX (PTE_R | PTE_W | PTE_X)  // 读/写/执行
#define PAGE_SYS (PTE_R | PTE_W | PTE_X)  // 系统级
#define PAGE_USR (PTE_R | PTE_W | PTE_X |PTE_U )  // 用户级
#define PAGE_DEV (PTE_R | PTE_W | PTE_X)  // 设备级

typedef u32 page_dir_t;

#endif