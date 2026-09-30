/*******************************************************************
* Copyright 2021-present evilbinary
* 作者: evilbinary on 01/01/20
* 邮箱: rootdebug@163.com
********************************************************************/
#ifndef IO_H
#define IO_H

#include "libs/include/types.h"
#define KPRINT_BUF 2048

int kprintf(const char* format, ...);

/* 逐字符写控制台（跳过打印锁/缓冲）—— bring-up 诊断与底层输出用 */
void print_char(u8 ch);

typedef void (*write_channel_fn)(u8 ch);
void io_add_write_channel(write_channel_fn fn);

/* 【打印锁是否允许用原子/独占指令】架构层在"内存属性是否 Normal"发生变化时
 * 设置，缺省 1（允许，保持历史行为）。armv7-a 必须在 MMU 关闭期间设为 0：
 * 此时全部内存是 Strongly-ordered，ldrex/strex 是 UNPREDICTABLE，Cortex-A7 实机
 * 会直接 data abort（现象与完整说明见 libs/libkernelcommon/io.c）。 */
void io_print_lock_set_atomic(int on);

#endif
