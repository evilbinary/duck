#include "gpio.h"
#include "hal/timer_ll.h"

typedef void (*rom_write_char_uart_fn)(char c);
rom_write_char_uart_fn send = 0x40007cf8;

void uart_send(unsigned int c) {
  send(c);
  //   if (c == '\n') {
  //     uart_send_ch(c);
  //     c = '\r';
  //   }
  //   uart_send_ch(c);
}

u32 timer_get_count() {
  u32 ret;
  asm volatile("rsr %0, ccount" : "=a"(ret) :);
  return ret;
}

void reset_count() { asm volatile("wsr %0, ccount; rsync" : : "a"(0)); }

void timer_init(int hz) {
  int group_num = 0;
  timg_dev_t* dev = TIMER_LL_GET_HW(group_num);
  int timer_num = 0;
  // enable peripheral clock
  timer_ll_enable_clock(dev, timer_num, true);
  // stop counter, alarm, auto-reload at first place
  timer_ll_enable_counter(dev, timer_num, false);
  timer_ll_enable_auto_reload(dev, timer_num, false);
  timer_ll_enable_alarm(dev, timer_num, false);

  timer_ll_enable_intr(dev, 0, true);

  reset_count();

  asm volatile("wsr %0, ccompare0; rsync" : : "a"(100));
  asm volatile("wsr %0, ccompare1; rsync" : : "a"(0));
  asm volatile("wsr %0, ccompare2; rsync" : : "a"(0));

  u32 ier;
  asm volatile("rsr %0, intenable" : "=a"(ier) : : "memory");
  asm volatile("wsr %0, intenable; rsync"
               :
               : "a"(ier | 0xff));
}

void timer_end() {
  // kprintf("timer end %d\n",timer_count);
}

void platform_init() { io_add_write_channel(&uart_send); }

void platform_end() { kprintf("platform_end %x\n", &uart_send); }

void platform_map() {}