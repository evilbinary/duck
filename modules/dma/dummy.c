/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "dma.h"
#include "kernel/kernel.h"

u32 dma_trans(u32 channel, void* src, void* dst, size_t len) { return 1; }

u32 dma_init(u32 channel, u32 mode, dma_interrupt_handler_t handler,
             void* data) {
  return -1;
}