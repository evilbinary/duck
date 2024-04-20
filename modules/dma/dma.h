/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef DMA_H
#define DMA_H

#include "kernel/kernel.h"

#define DMA_MODE_READ_TRANSFER 4
#define DMA_MODE_WRITE_TRANSFER 8

#define DMA_MODE_TRANSFER_SINGLE 0x40
#define DMA_MODE_TRANSFER_BLOCK 0x80
#define DMA_MODE_TRANSFER_CASCADE 0xC0

typedef void(dma_interrupt_handler_t)(void *);

u32 dma_init(u32 channel, u32 mode, dma_interrupt_handler_t handler);

u32 dma_trans(u32 channel, void *src, void *dst, size_t len);

// void dma_stop(u32 channel);

#endif