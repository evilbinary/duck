/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "../display.h"

#include "../boot.h"
#include "../lock.h"
#include "gpio.h"
extern boot_info_t* boot_info;

void cls() {}
void putch(u8 c) { uart_send(c); }

void puts(char* text) {
  for (; *text != 0; text++) {
    putch(*text);
  }
}

/* bring-up 用：直写串口的十六进制打印（和 puts 同通路，绕过 io 通道/日志锁）。
 * 用途：在 kprintf 通路可疑时把关键指针/寄存器值打出来。 */
void puthex(unsigned long v) {
  static const char hx[] = "0123456789abcdef";
  int started = 0;
  for (int i = 60; i >= 0; i -= 4) {
    unsigned d = (unsigned)((v >> i) & 0xf);
    if (!started && d == 0 && i != 0) continue;
    started = 1;
    putch(hx[d]);
  }
}

void display_init() {
  puts("Hello,YiYiYa OS\n\r");
  puts("display init\n\r");
  if (boot_info->disply.mode == 1) {
  }
}
