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
/* ---- socket 的 vnode 操作表 ----
 * 方案 A：socket 也走统一 vnode+op 抽象，和串口/键盘一致。
 * 每个连接一个 vnode，lwip fd 存进 node->data（u32）。
 * 好处：fd->data 恒为合法 vnode ⇒ sys_read/sys_write 统一走 vread/vwrite，
 * 不再需要 type/name 特判（消除"把标记当 vnode 跳野地址"的 bug）。
 * 注意：vread/vwrite 会持 vfs 全局锁调用本 op，所以此处必须非阻塞
 * （lwip 无数据返回 EAGAIN，由用户态重试），不能长阻塞。 */
static int socket_node_lwfd(vnode_t* node) {
  if (node == NULL) return -1;
  return (int)(uintptr_t)node->data;
}

static u32 socket_node_read(vnode_t* node, u32 offset, u32 size, u8* buffer) {
  long n = lwip_port_recvfrom(socket_node_lwfd(node), buffer, size, 0, NULL, NULL);
  (void)offset;
  /* 保留负错误码语义：负值强转 u32 是巨大值，musl __syscall_ret 会识别为
   * error（r > -4096）并转成 -1+errno。EAGAIN(-11) 才能让用户态正确重试，
   * 不能吞成 0（0 会被当 EOF 退出读取循环）。 */
  return (u32)n;
}

extern void ethernetif_pump(void);

static u32 socket_node_write(vnode_t* node, u32 offset, u32 size, u8* buffer) {
  long n;
  n = lwip_port_sendto(socket_node_lwfd(node), buffer, size, 0, NULL, 0);
  (void)offset;
  /* 【EAGAIN 必须主动收包】发送缓冲满后要等对端 ACK 才能腾出空间，而 ACK
   * 处理靠 tcpip 线程收包。socket 写在系统调用上下文（SVC 屏蔽 IRQ）里，
   * 用户态 usleep 重试期间 tcpip 得不到调度 ⇒ 缓冲永不释放、无限 EAGAIN
   * （实测 off 卡死在 9896 不动）。这里 EAGAIN 时 pump 一拍收 ACK。
   * pump 里 LOCK_TCPIP_CORE 若被 tcpip 持有，spin_lock 的 daifclr 会放行
   * （sys_arch.c），不会像以前那样死等。 */
  if (n < 0) {
    ethernetif_pump();
  }
  return (u32)n;
}

static u32 socket_node_ioctl(vnode_t* node, u32 cmd, void* args) {
  (void)node;
  (void)cmd;
  (void)args;
  /* 【未知命令必须报错】本节点的 ioctl 全部未实现，返回 0 是假成功
   * （LTP ioctl_pidfd01 期望 ENOTTY；调用方会把成功误当真）。 */
  return (u32)-1;
}

static u32 socket_node_close(vnode_t* node) {
  if (node == NULL) return 0;
  if (node->data != NULL) {
    lwip_port_close_lwfd((int)(uintptr_t)node->data);
    node->data = NULL;
  }
  /* vfs_create_node 分配了 node->name，需一并归还，避免每次连接泄漏。 */
  if (node->name != NULL) {
    kfree(node->name);
    node->name = NULL;
  }
  kfree(node);
  return 0;
}

const voperator_t socket_operator = {
    .read = socket_node_read,
    .write = socket_node_write,
    .close = socket_node_close,
    .ioctl = socket_node_ioctl,
};
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
  /* 协议栈在驱动之上。失败只少一张网卡，socket 调用会返回错误。
   * socket 的关闭由 vnode 的 socket_operator.close 完成（见 fd_close →
   * vclose），不再需要 fd_close_listen 回调。 */
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
