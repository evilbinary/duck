/*******************************************************************
* Copyright 2021-present evilbinary
* 作者: evilbinary on 01/01/20
* 邮箱: rootdebug@163.com
********************************************************************/
#ifndef ARCH_DISPLAY_H
#define ARCH_DISPLAY_H

#include "libs/include/types.h"

void display_init();

#define debug kprintf
#define log kprintf
#define info kprintf

#endif