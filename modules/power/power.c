/*******************************************************************
* Copyright 2021-present evilbinary
* 作者: evilbinary on 01/01/20
* 邮箱: rootdebug@163.com
********************************************************************/
#include "kernel/kernel.h"



int power_init(void) {
  kprintf("power init\n");

  device_t* dev = kmalloc(sizeof(device_t),DEFAULT_TYPE);
  dev->name = "power";
  dev->read = NULL;
  dev->write = NULL;
  dev->ioctl = NULL;
  dev->id = DEVICE_POWER;
  dev->type = DEVICE_TYPE_CHAR;
  device_add(dev);
  
  power_init_device(dev);

  // net
  vnode_t *net = vfs_create_node("power", V_FILE | V_BLOCKDEVICE);
  net->device = device_find(DEVICE_NET);
  net->op = &device_operator;
  vfs_mount(NULL, "/dev", net);
  return 0;
}

void power_exit(void) { kprintf("power exit\n"); }


module_t power_module = {
    .name ="power",
    .init=power_init,
    .exit=power_exit
};
