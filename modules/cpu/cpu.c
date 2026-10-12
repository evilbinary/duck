/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
/* Raspberry Pi 5 (BCM2712) CPU 温度模块（独立于风扇，见 duck/modules/fan）。
 *
 *  两个独立单元：温度走
 *   firmware mailbox（VideoCore 侧传感器），不依赖 RP1/PCIe2 链路，任何时候
 *   都可读；而风扇在 RP1 南桥，链路没起来就整个不可用。两者的可用性、
 *   失败模式、调用方（温控策略读温度、控风扇）都不同，拆开各自独立。
 *
 * 【走 firmware mailbox 属性通道】tag GET_TEMPERATURE=0x00030006（与
 *   bcm2835_vc.h 的 BCM2835_VC_TAG_GET_TEMP 同值；这里自带定义，避免依赖
 *   mod-mmc）。Pi5 上 mailbox 传 CPU 物理地址（GPU_MEM_BASE=0，见
 *   bcm2835.h RASPI5 分支），buffer 必须 16 字节对齐且物理 <4GB，
 *   发送前 clean、接收后 invalidate（固件是另一个 cache master）。
 *
 * 【接口】/dev/cpu：read 4 字节 = 毫摄氏度（读失败返回 0 字节）。
 ********************************************************************/
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "dev/devfs.h"
#include "bcm/mailbox.h"

/* firmware mailbox 温度 tag（同 bcm2835_vc.h 的 GET_TEMP） */
#define VC_TAG_GET_TEMPERATURE 0x00030006

/* 属性通道消息：header(8) + tag(20) + end(4)，16 字节对齐给固件 */
typedef struct __attribute__((aligned(16))) {
  u32 size;
  u32 req_code; /* 请求填 0；响应 bit31=1 表示成功 */
  u32 tag;
  u32 buf_size;
  u32 data_size;
  u32 dev_id;
  u32 value; /* 响应：毫摄氏度 */
  u32 end;
} mbox_temp_msg_t;

/* Pi5：mailbox 传 CPU 物理地址（<4GB）。kmalloc 出来的若是高地址就报错 ——
 * bcm2712.c 同款约束（VideoCore 只有 32 位总线） */
static int cpu_read_temp(int* milli_celsius) {
  mbox_temp_msg_t* msg =
      kmalloc_alignment(sizeof(mbox_temp_msg_t), 16, DEVICE_TYPE);
  if (msg == NULL) return -1;

  msg->size = sizeof(mbox_temp_msg_t);
  msg->req_code = 0;
  msg->tag = VC_TAG_GET_TEMPERATURE;
  msg->buf_size = 8;
  msg->data_size = 4;
  msg->dev_id = 0;
  msg->value = 0;
  msg->end = 0;

  int ret = -1;
  if (((uintptr_t)msg & 0xFFFFFFFF00000000ULL) != 0) {
    kprintf("cpu: temp buffer above 4GB, skip\n");
    kfree(msg);
    return -1;
  }

  void* pa = kpage_v2p(msg, 0);
  if (pa == NULL) pa = msg; /* 已在物理直映射区 */
  u32 phys = (u32)(uintptr_t)pa;

  /* 固件是另一个 master：发送前 clean，接收后 invalidate（同 bcm2712.c） */
  cpu_flush_dcache_range((unsigned long)msg,
                         (unsigned long)msg + sizeof(mbox_temp_msg_t));
  mailbox_write_read(BCM2835_MAILBOX_PROP_CHANNEL, phys);
  cpu_flush_dcache_range((unsigned long)msg,
                         (unsigned long)msg + sizeof(mbox_temp_msg_t));

  if (msg->req_code == BCM2835_MAILBOX_SUCCESS && msg->data_size == 4) {
    *milli_celsius = (int)msg->value;
    ret = 0;
  }
  kfree(msg);
  return ret;
}

/* /dev/cpu：read 4 字节 = 毫摄氏度 */
static size_t cpu_read(device_t* dev, void* buf, size_t len) {
  int mc;
  if (len < sizeof(int)) return 0;
  if (cpu_read_temp(&mc) != 0) return 0;
  *(int*)buf = mc;
  return sizeof(int);
}

/* 【函数名不能用 cpu_init】duck/arch 下已有同名的 arch 层初始化函数，
 * 链接期会 conflicting types；模块实现统一加 _temp 前缀 */
int cpu_temp_init(void) {
  device_t* cpu_dev = kmalloc(sizeof(device_t), DEFAULT_TYPE);
  cpu_dev->name = "cpu";
  cpu_dev->read = cpu_read;
  cpu_dev->id = DEVICE_CPU;
  cpu_dev->type = DEVICE_TYPE_CHAR;
  device_add(cpu_dev);

  vnode_t* vnode = vfs_create_node("cpu", V_FILE);
  vfs_mount(NULL, "/dev", vnode);
  vnode->device = device_find(DEVICE_CPU);
  vnode->op = &device_operator;

  return 0;
}

void cpu_temp_exit(void) {}

module_t cpu_module = {.name = "cpu",
                       .init = cpu_temp_init,
                       .exit = cpu_temp_exit};
