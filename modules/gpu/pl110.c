/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "pl110.h"

#include "vga/vga.h"

int gpu_init_mode(vga_device_t *vga, int mode) {
  log_debug("pl110 init\n");
  vga->mode = mode;
  vga->write = NULL;

  vga->width = 640;
  vga->height = 480;

  // vga->width = 800;
  // vga->height = 600;

  vga->bpp = 24;

  vga->framebuffer_index = 0;
  vga->framebuffer_count = 1;
  vga->pframbuffer = 0xfb0000;
  vga->frambuffer = vga->pframbuffer;

  vga->framebuffer_length = vga->width * vga->height;

  vga->flip_buffer = NULL;
  pl110_lcd_init(vga);

  log_info("fb addr:%x end:%x len:%x\n", vga->frambuffer,
           vga->frambuffer + vga->framebuffer_length, vga->framebuffer_length);

  // u32 *buffer = vga->frambuffer;
  // for (int i = 0; i < vga->framebuffer_length / 4; i++) {
  //   buffer[i] = 0x000000;
  // }

  return 0;
}

int pl110_lcd_init(vga_device_t *vga) {
  log_info("pl110_lcd_init\n");

  pl110_lcd *lcd = kmalloc(sizeof(pl110_lcd), DEFAULT_TYPE);
  vga->priv = lcd;

  lcd->width = vga->width;
  lcd->height = vga->height;
  lcd->bits_per_pixel = 18;
  lcd->bytes_per_pixel = 4;

  page_map(VERSATILEPB_OSC1, VERSATILEPB_OSC1, 0);

  page_map(VERSATILEPB_PL110_LCD_BASE, VERSATILEPB_PL110_LCD_BASE, 0);

  if (lcd->width == 640 && lcd->height == 480) {
    *(volatile u32 *)(VERSATILEPB_OSC1) = 0x2C77;
    *(volatile u32 *)(VERSATILEPB_PL110_LCD_BASE + LCD_TIMING0) = 0x3f1f3f9c;
    *(volatile u32 *)(VERSATILEPB_PL110_LCD_BASE + LCD_TIMING1) = 0x090b61df;
    *(volatile u32 *)(VERSATILEPB_PL110_LCD_BASE + LCD_TIMING2) = 0x067f1800;
  } else if (lcd->width == 800 && lcd->height == 600) {
    *(volatile u32 *)(VERSATILEPB_OSC1) = 0x00002CAC;
    *(volatile u32 *)(VERSATILEPB_PL110_LCD_BASE + LCD_TIMING0) = 0x1313A4C4;
    *(volatile u32 *)(VERSATILEPB_PL110_LCD_BASE + LCD_TIMING1) = 0x0505F6F7;
    *(volatile u32 *)(VERSATILEPB_PL110_LCD_BASE + LCD_TIMING2) = 0x071F1800;
  } else {
    log_error("not support %d %d\n", lcd->width, lcd->height);
  }

  *(volatile u32 *)(VERSATILEPB_PL110_LCD_BASE + LCD_UPBASE) = vga->frambuffer;
  *(volatile u32 *)(VERSATILEPB_PL110_LCD_BASE + LCD_IMSC) = 0x82B;

  log_debug("lcd %dx%d len= %d\n", lcd->width, lcd->height,
            vga->framebuffer_length);

  // map fb
  u32 addr = vga->frambuffer;
  u32 paddr = vga->pframbuffer;
  log_debug("map fb start %x %x\n", addr, paddr);

  for (int i = 0; i < vga->framebuffer_length / PAGE_SIZE; i++) {
    page_map(addr, paddr, PAGE_DEV);
    addr += PAGE_SIZE;
    paddr += PAGE_SIZE;
  }
  log_debug("map fb end %x %x\n", addr, paddr);

  log_info("pl110_lcd_init end\n");

  return 0;
}
