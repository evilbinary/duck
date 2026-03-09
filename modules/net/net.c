/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "kernel/kernel.h"
#include "dev/devfs.h"

// Platform-specific includes
#if defined(RASPI2) || defined(RASPI3)
#include "bcm2837.h"
#endif

// Forward declarations for platform-specific init
#if defined(RASPI2) || defined(RASPI3)
#define NET_INIT_DEVICE bcm2837_net_init
#else
extern int net_init_device(device_t* dev);  // e1000 for x86/PCI systems
#define NET_INIT_DEVICE net_init_device
#endif

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
  
  // Initialize platform-specific network device
  #if defined(RASPI2) || defined(RASPI3)
    dev->read = bcm2837_net_read;
    dev->write = bcm2837_net_write;
    dev->ioctl = bcm2837_net_ioctl;
    bcm2837_net_init(dev);
  #else
    net_init_device(dev);
  #endif

  // Create /dev/net device node
  vnode_t *net = vfs_create_node("net", V_FILE | V_BLOCKDEVICE);
  net->device = device_find(DEVICE_NET);
  net->op = &device_operator;
  vfs_mount(NULL, "/dev", net);

  return 0;
}

void net_exit(void) { kprintf("net exit\n"); }

module_t net_module = {.name = "net", .init = net_init, .exit = net_exit};
