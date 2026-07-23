/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef X86_MM_H
#define X86_MM_H

#define PAGE_R  0
#define PAGE_P   1
#define PAGE_RX   0 //读执行
#define PAGE_RW   (PAGE_P | PAGE_USR | PAGE_RWX)
#define PAGE_RWX   2 //读/写/执行
#define PAGE_SYS   0 //系统级
#define PAGE_USR   4 //用户级
typedef u64* page_dir_t;

#define PAGE_RW_NC PAGE_RW
#define PAGE_DEV PAGE_RW

void* page_v2p(u64* page_dir_ptr_tab, void* vaddr);

#endif