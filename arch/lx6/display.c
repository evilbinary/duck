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


void set_cursor(int x, int y) {
  
}
void cls() {

}
void putch(u8 c) {
  uart_send(c);
}

void puts(char* text) {
  for (; *text != 0; text++) {
    uart_send(*text);
  }
}

void test_display() {
  cls();
  puts("test display hello,YiYiYa\n\r");
}

void display_init() {
  //uart_send('a');
  // puts("Hello,YiYiYa OS\n\r");
  // uart_send('b');
  // puts("display init\n\r");
  // test_display();
}
