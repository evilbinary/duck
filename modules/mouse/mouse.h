/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef MOUSE_H
#define MOUSE_H

#include "kernel/kernel.h"

#define MOUSE_DATA 0x60
#define MOUSE_STATUS 0x64
#define MOUSE_COMMAND 0x64

#define EVENT_NUMBER 64

typedef struct mouse_event{
  u32 sate;
  i32 x;
  i32 y;
}mouse_event_t;

typedef struct mouse_device {
  queue_pool_t* events;
  void* data;
  u32 event_index;   /* 写索引：中断里投递事件后 +1（生产者） */
  u32 read_index;    /* 读索引：read() 取走一个事件后 +1（消费者）。
                      * 【必须有】没有它，read() 无法判断"有没有新事件"，
                      * 就只能无条件返回数据 —— 调用方 xinput_poll 是
                      * `while (read(...) >= 3)`，会死循环（见 pl050.c read） */
  u8 packet[4];
  u32 packet_index;
  u32 btn_old;

  u32 x;
  u32 y;
 mouse_event_t event_data[4];
  /* 每个事件槽对应的原始 PS/2 包副本：xwin 的 xinput_poll 用 len=3 读取，
   * 交给 xinput_ps2_mouse_data 解析（相对位移）；而 /dev/mouse 用 len>=12 读
   * 绝对事件。同一驱动服务两类调用方，由 read() 按 len 分派。 */
  u8 packet_data[4][4];

} mouse_device_t;

#endif