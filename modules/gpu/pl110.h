#ifndef PL110_H
#define PL110_H

#include "kernel/kernel.h"
#include "libs/include/types.h"

#define VERSATILEPB_OSC1 0x1000001c
#define VERSATILEPB_PL110_LCD_BASE 0x10120000
#define VERSATILEPB_PL110_LCD_TIME_REG0 0x10120000
#define VERSATILEPB_PL110_LCD_TIME_REG1 0x10120004
#define VERSATILEPB_PL110_LCD_TIME_REG2 0x10120008

#define LCD_TIMING0 0x0
#define LCD_TIMING1 0x4
#define LCD_TIMING2 0x8
#define LCD_TIMING3 0xC
#define LCD_UPBASE 0x10
#define LCD_IMSC 0x18

typedef struct pl110_lcd {
  int width;
  int height;
  int pwidth;
  int pheight;
  int bits_per_pixel;
  int bytes_per_pixel;

} pl110_lcd;

#endif
