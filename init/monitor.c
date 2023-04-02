/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "main.h"

int module_ready=0;

void do_kernel_thread(void) {
  modules_init();
  log_info("mp init\n");
  mp_init();

  module_ready=1;
  
  u32 i = 0;
  u32 count = 0;
  for (;;) {
    count++;
    if (i % 4 == 0) {
      i = 0;
    }
    // cpu_wait();
  }
}

void do_monitor_thread(void) {
  u32 i = 0;
  u32 count = 0;
  for (;;) {
    count++;
    if (i % 4 == 0) {
      i = 0;
    }
    // cpu_wait();
  }
}
