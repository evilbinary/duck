/*******************************************************************
* Copyright 2021-present evilbinary
* 作者: evilbinary on 01/01/20
* 邮箱: rootdebug@163.com
********************************************************************/
#ifndef IO_H
#define IO_H

#include "libs/include/types.h"
#define KPRINT_BUF 512

int kprintf(const char* format, ...);

typedef void (*write_channel_fn)(u8 ch);
void io_add_write_channel(write_channel_fn fn);

#endif
