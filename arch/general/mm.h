/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef MM_H
#define MM_H

#define PAGE_P   0
#define PAGE_RWR   0 //读执行
#define PAGE_RWW   0 //读/写/执行
#define PAGE_USS    0//系统级
#define PAGE_USU    0//用户级
#define PAGE_DEV    0//设备级

#define PAGE_RWE   0
#define PAGE_RW   0

typedef u32 page_dir_t;

#endif