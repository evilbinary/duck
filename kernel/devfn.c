/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/

#include "devfn.h"

#include "kernel.h"

size_t dev_ioctl(int fd, u32 cmd, ...) {
  u32 ret = 0;
  device_t *dev = device_find(fd);
  if (dev == NULL) {
    return ret;
  }
  va_list args;
  va_start(args, cmd);
  ret = dev->ioctl(dev, cmd, args);
  va_end(args);
  return ret;
}

size_t dev_read(int fd, void *buf, size_t nbytes) {
  device_t *dev = device_find(fd);
  return dev->read(dev, buf, nbytes);
}

size_t dev_write(int fd, void *buf, size_t nbytes) {
  device_t *dev = device_find(fd);
  return dev->write(dev, buf, nbytes);
}

u32 device_read(vnode_t *node, u32 offset, size_t nbytes, u8 *buffer) {
  u32 ret = 0;
  if(node==NULL){
    return ret;
  }
  device_t *dev = (device_t *)node->device;
  if (dev == NULL || dev->read == NULL) {
    return ret;
  }
  ret = dev->read(dev, buffer, nbytes);
  return ret;
}

u32 device_write(vnode_t *node, u32 offset, size_t nbytes, u8 *buffer) {
  u32 ret = 0;
  device_t *dev = (device_t *)node->device;
  if (dev == NULL || dev->write == NULL) {
    return ret;
  }
  ret = dev->write(dev, buffer, nbytes);
  return ret;
}

size_t device_ioctl(vnode_t *node, u32 cmd, void *args) {
  u32 ret = 0;
  device_t *dev;
  /* 【防御 + 定位】原写法直接 `node->device`：
   *   · node 为 NULL 时解引用 0 ⇒ memory fault at 0；
   *   · dev 若是野指针，下面的 `dev->ioctl == NULL` 也拦不住
   *     （从垃圾内存读出来的可能是 0/野值，检查通过后跳过去就崩）。
   * 实测 raspi3：/bin/logo 在 openat 之后的 ioctl 路径崩在 0
   *   （pc=device_ioctl+0x64 / devfn.c:69，lr=vioctl+0xc8 / vfs.c:85）。
   * 内核代码段在 0x100000 之上 ⇒ 函数指针落在其下必是"未初始化/被踩"。
   * 这里只做防御：宁可让该 ioctl 静默失败，也不让内核踩 0。 */
  /* 【AArch64 ABI 说明】形参必须是 void*（见 devfn.h 的注释）：以前声明成
   * va_list 时，编译器会去 *读 args 指向的 32 字节* 来构造 va_list 副本，
   * 无参 ioctl（args=NULL）⇒ memory fault at 0。 */
  if (node == NULL || node->device == NULL) {
    return ret;
  }
  dev = (device_t *)node->device;
  if (dev->ioctl == NULL || (u64)(uintptr_t)dev->ioctl < 0x100000UL) {
    //log_debug("device %s ioctl null\n", node->name);
    return ret;
  }
  // va_list args;
  // va_start(args, cmd);
  ret = dev->ioctl(dev, cmd, args);
  // va_end(args);
  return ret;
}

u32 device_open(vnode_t *node) {
  u32 ret = 0;
  device_t *dev = (device_t *)node->device;
  if (dev == NULL) {
    return ret;
  }
  // todo
  // ret = dev->close(dev, buffer, nbytes);
  return ret;
}

u32 device_close(vnode_t *node) {
  u32 ret = 0;
  device_t *dev = (device_t *)node->device;
  if (dev == NULL) {
    return ret;
  }
  // todo
  // ret = dev->close(dev, buffer, nbytes);
  return ret;
}