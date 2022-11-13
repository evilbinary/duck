/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "kernel/kernel.h"
#include "dev/devfs.h"

int net_init(void) {
  kprintf("net init\n");
  device_t* dev = kmalloc(sizeof(device_t),DEFAULT_TYPE);
  dev->name = "net";
  dev->read = NULL;
  dev->write = NULL;
  dev->ioctl = NULL;
  dev->id = DEVICE_NET;
  dev->type = DEVICE_TYPE_NET;
  device_add(dev);
  
  net_init_device(dev);

  // net
  vnode_t *net = vfs_create_node("net", V_FILE | V_BLOCKDEVICE);
  net->device = device_find(DEVICE_NET);
  net->op = &device_operator;
  vfs_mount(NULL, "/dev", net);

  return 0;
}

void net_exit(void) { kprintf("net exit\n"); }

module_t net_module = {.name = "net", .init = net_init, .exit = net_exit};
