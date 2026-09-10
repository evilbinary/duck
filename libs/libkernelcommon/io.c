/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "io.h"
#include "common.h"
#include "string.h"

#include "stdarg.h"

write_channel_fn write_channels[10];
u32 write_channel_number = 0;

void io_add_write_channel(write_channel_fn fn) {
  for (int i = 0; i < write_channel_number; i++) {
    if (fn == write_channels[i]) {
      return;
    }
  }
  write_channels[write_channel_number++] = fn;
}

void print_char(u8 ch) {
  for (int i = 0; i < write_channel_number; i++) {
    write_channel_fn fn = write_channels[i];
    if (fn != NULL) {
      fn(ch);
    }
  }
}

char printf_buffer[KPRINT_BUF];

/* 所有 CPU 共用 printf_buffer 和同一个控制台：并发打印会互相覆盖字符串，
 * 实测日志被打成乱码（探针数据完全取不出来）。这里用编译器内置原子操作
 * 加锁——平台无关，不涉及任何架构专有指令（库层/通用层都应如此）。
 * 有界自旋：持有者卡死（例如同 CPU 在异常里重入打印）时放弃抢锁，
 * 宁可输出交错也绝不死锁。 */
static volatile int print_lock;

int kprintf(const char* fmt, ...) {
  int locked = 0;
  int spins = 0;
  while (__sync_lock_test_and_set(&print_lock, 1)) {
    if (++spins > 200000) break;
  }
  locked = (spins <= 200000);

  kmemset(printf_buffer, 0, KPRINT_BUF);
  int i = 0;
  va_list args;
  va_start(args, fmt);
  i = kvsprintf(printf_buffer, fmt, args);
  va_end(args);

  int len = kstrlen(printf_buffer);
  if (i > KPRINT_BUF) {
    len = KPRINT_BUF-1;
    // OVER PRINT
    print_char('O');
    print_char('V');
    print_char('E');
    print_char('R');
    print_char(' ');
    print_char('P');
    print_char('R');
    print_char('I');
    print_char('N');
    print_char('T');
    print_char('\n');
  }

  for (int i = 0; i < len; i++) {
    print_char(printf_buffer[i]);
  }
  if (locked) {
    __sync_lock_release(&print_lock);
  }
  return i;
}