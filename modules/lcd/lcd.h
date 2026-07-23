/*******************************************************************
* Copyright 2021-present evilbinary
* 作者: evilbinary on 01/01/20
* 邮箱: rootdebug@163.com
********************************************************************/
#ifndef LCD_H
#define LCD_H

#include "kernel/kernel.h"
#include "vga/vga.h"


void lcd_fill(u16 xsta, u16 ysta, u16 xend, u16 yend, u16 color);

#endif
