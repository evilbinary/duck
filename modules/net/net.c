/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "kernel/kernel.h"
#include "dev/devfs.h"
#ifdef NET_DRIVER
#include "lwip_port.h"
#endif

// 平台特定的初始化函数由各个驱动文件实现
// raspi2/raspi3: net_init_device() in bcm2837.c
// x86/qemu: net_init_device() in e1000.c
// v3s: net_init_device() in v3s.c（片上 EMAC + 内置 10/100M PHY）
// raspi5: net_init_device() in rp1_gem.c（RP1 Cadence GEM）
//
// 【注意不要在这里放 weak 默认实现】本模块是静态库：一旦 net_module 被引用，链接器
// 就会拉入 net.o，此时若本文件里有 weak 的 net_init_device，引用就被"满足"了，
// 平台驱动所在的 v3s.o/bcm2837.o 永远不会被拉进来（实测 kernel.elf 里
// net_init_device 停在 W，EMAC 完全没初始化）。所以注册侧用 NET_DRIVER 宏限定：
// 只有真的有网卡驱动的平台才注册 net 模块（见根 ya.py 与 app/init/module.c）。
extern int net_init_device(device_t* dev);

#ifdef NET_DRIVER
static int net_name_is(fd_t* fd, const char* s) {
  const char* n;
  if (fd == NULL || fd->name == NULL || s == NULL) return 0;
  n = (const char*)fd->name;
  while (*s != 0 && *n != 0 && *s == *n) {
    s++;
    n++;
  }
  return *s == 0 && *n == 0;
}

/* socket 的 data 是 kmalloc 出来的标记，不是 vnode。 */
static int net_fd_on_close(fd_t* fd) {
  if (!net_name_is(fd, "socket")) return 0;
  lwip_port_close_lwfd((int)fd->offset);
  if (fd->data != NULL) {
    kfree(fd->data);
    fd->data = NULL;
  }
  return 1;
}
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
  
  // 调用平台特定的初始化函数
  net_init_device(dev);
#ifdef NET_DRIVER
  /* 协议栈在驱动之上。失败只少一张网卡，socket 调用会返回错误。 */
  fd_close_listen(net_fd_on_close);
  lwip_port_init();
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
