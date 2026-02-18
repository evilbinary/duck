/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "../arch.h"

// 多核CPU初始化 (Application Processor init)
void ap_init(int cpu) {
  cpu_init();
  interrupt_init();
}
