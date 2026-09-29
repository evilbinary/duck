/*******************************************************************
* Copyright 2021-present evilbinary
* 作者: evilbinary on 01/01/20
* 邮箱: rootdebug@163.com
********************************************************************/
#ifndef DEVCFN_H
#define DEVCFN_H

#include "vfs.h"


size_t dev_ioctl(int fd, u32 cmd, ...);

size_t dev_read(int fd, void* buf, size_t nbytes);

size_t dev_write(int fd, void* buf, size_t nbytes);


u32 device_write(vnode_t *node, u32 offset, size_t nbytes, u8 *buf);
u32 device_read(vnode_t *node, u32 offset, size_t nbytes, u8 *buf);
/* 【必须是 void*，不能是 va_list】vfs.h 的 voperator_t.ioctl 槽位就是 void*；
 * AArch64 上 va_list 是"结构体"（i386/ARM32 才是指针）：按 va_list 声明形参时，
 * 编译器会在调用驱动前【从该指针读 32 字节】去构造副本 ⇒ 无参 ioctl 传 NULL 时
 * 直接 memory fault at 0（实测 raspi3：/bin/logo 的 openat 后续 ioctl，
 * pc=device_ioctl+0x8c 的 ldp q0,q1,[x1]）。改成 void* 即与虚表/驱动一致。 */
size_t device_ioctl(vnode_t *node, u32 cmd, void *args);
u32 device_close(vnode_t *node) ;
u32 device_open(vnode_t *node);

#endif