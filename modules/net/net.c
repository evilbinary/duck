/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "kernel/kernel.h"
#include "dev/devfs.h"

// 平台特定的初始化函数由各个驱动文件实现
// raspi2/raspi3: net_init_device() in bcm2837.c
// x86/qemu: net_init_device() in e1000.c
extern int net_init_device(device_t* dev);

int net_init(void) {
  kprintf("net init\n");
  device_t* dev = kmalloc(sizeof(device_t), DEFAULT_TYPE);
  dev->name = "net";
  dev->read = NULL;
  dev->write = NULL;
  dev->ioctl = NULL;
  dev->id = DEVICE_NET;
  dev->type = DEVICE_TYPE_NET;
  device_add(dev);
  
  // 调用平台特定的初始化函数
  net_init_device(dev);

  // Create /dev/net device node
  vnode_t *net = vfs_create_node("net", V_FILE | V_BLOCKDEVICE);
  net->device = device_find(DEVICE_NET);
  net->op = &device_operator;
  vfs_mount(NULL, "/dev", net);

  return 0;
}

void net_exit(void) { kprintf("net exit\n"); }

module_t net_module = {.name = "net", .init = net_init, .exit = net_exit};
