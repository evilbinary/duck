/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef MM_H
#define MM_H

#define PAGE_P   0
#define PAGE_RX   0 //读执行
#define PAGE_RW   0
#define PAGE_RWX   0 //读/写/执行
#define PAGE_SYS    0//系统级
#define PAGE_USR    0//用户级
#define PAGE_DEV    0//设备级

typedef u32 page_dir_t;

#define PAGE_RW_NC PAGE_RW
#endif