/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "../arch.h"

__attribute__((weak)) void lcpu_wait_start(int cpu) {
  (void)cpu;
}

void ap_init(boot_info_t* boot, int cpu) {
  cpu_init(boot, cpu);
  ipi_clear(cpu);
  interrupt_init(cpu);
}
