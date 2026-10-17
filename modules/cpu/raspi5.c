/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
/* raspberry pi 5 (BCM2712) CPU 温度后端：firmware mailbox 属性通道。
 *
 * 为什么单独一个模块（不跟风扇放一起，见 duck/modules/fan）：
 *   温度走 VideoCore 固件 mailbox，不依赖 RP1/PCIe2 链路，任何时候都能读；
 *   风扇在 RP1 南桥，链路没起来就整个不可用。两者的可用性、失败模式、
 *   调用方（温控策略读温度、控风扇）都不同，拆开各自独立。
 *
 * 【tag】GET_TEMPERATURE=0x00030006（与 bcm2835_vc.h 的
 *   BCM2835_VC_TAG_GET_TEMP 同值；这里自带定义，避免依赖 mod-mmc）。
 *
 * 【buffer 约束】Pi5 上 mailbox 传 CPU 物理地址（GPU_MEM_BASE=0，见
 *   bcm2835.h RASPI5 分支），buffer 必须 16 字节对齐且物理地址 <4GB。
 *
 * 【cache】固件是另一个 cache master：发送前 clean（civac）让请求落到 DRAM；
 *   接收后必须【仅 invalidate】（ivac）—— 若再用 clean+invalidate 会把 CPU
 *   cache 里的旧内容写回 DRAM，覆盖固件刚写的响应（真机实测 temperature
 *   read failed 的根因）。
 ********************************************************************/
#include "cpu.h"
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

/* 【必须用 kfree_alignment】kmalloc_alignment 返回的是对齐切出来的指针，
 * ya 堆块头在原始指针 p2[-1] 上；kfree() 会把对齐指针当块数据解析
 * → magic 错乱（真机实测 ya_real_size invalid block / mfree error）。 */
int cpu_temp_read(int* milli_celsius) {
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
    kfree_alignment(msg);
    return -1;
  }

  void* pa = kpage_v2p(msg, 0);
  if (pa == NULL) pa = msg; /* 已在物理直映射区 */
  u32 phys = (u32)(uintptr_t)pa;

  cpu_flush_dcache_range((unsigned long)msg,
                         (unsigned long)msg + sizeof(mbox_temp_msg_t));
  mailbox_write_read(BCM2835_MAILBOX_PROP_CHANNEL, phys);
  cpu_invalidate_dcache_range((unsigned long)msg,
                              (unsigned long)msg + sizeof(mbox_temp_msg_t));

  /* 【响应格式】req_code 位31=1 表示固件成功；data_size 位31=1 表示"已填充"，
   * 低 31 位是有效字节数（真机实测 0x80000008：头 4 + 值 4）。
   * 之前按 data_size == 4 判断，把正常的 0x80000008 当失败 —— 温度其实
   * 一直在 msg->value 里（真机 36730 = 36.73°C）。 */
  if (msg->req_code == BCM2835_MAILBOX_SUCCESS &&
      (msg->data_size & 0x80000000u)) {
    *milli_celsius = (int)msg->value;
    ret = 0;
  } else {
    kprintf("cpu: mbox resp req=%08x data_size=%08x val=%d\n", msg->req_code,
            msg->data_size, msg->value);
  }
  kfree_alignment(msg);
  return ret;
}
