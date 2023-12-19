/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "gpu.h"
#include "kernel/kernel.h"
#include "vga/vga.h"

int ssd202_lcd_init(vga_device_t *vga) {
  u32 addr = vga->frambuffer;
  u32 paddr = vga->pframbuffer;
  for (int i = 0; i < vga->framebuffer_length / PAGE_SIZE; i++) {
    page_map(addr, paddr, PAGE_DEV);
    addr += 0x1000;
    paddr += 0x1000;
  }

  log_info("fb addr:%x end:%x len:%x\n", vga->frambuffer,vga->frambuffer+vga->framebuffer_length, vga->framebuffer_length);

  // u8 *buffer = vga->frambuffer;
  //   for (int i = 0; i < vga->framebuffer_length / 8; i++) {
  //     buffer[i] = 0xff;
  // }


  return 0;
}

int gpu_init_mode(vga_device_t *vga, int mode) {
  if (mode == VGA_MODE_80x25) {
    vga->width = 80;
    vga->height = 25;
  } else if (mode == VGA_MODE_320x200x256) {
    vga->width = 320;
    vga->height = 256;
  } else if (mode == VGA_MODE_640x480x24) {
    vga->width = 640;
    vga->height = 480;
    vga->bpp = 24;
  } else if (mode == VGA_MODE_480x272x24) {
    vga->width = 480;
    vga->height = 272;
    vga->bpp = 24;
  } else if (mode == VGA_MODE_480x272x32) {
    vga->width = 480;
    vga->height = 272;
    vga->bpp = 32;
  } else if (mode == VGA_MODE_480x272x18) {
    vga->width = 480;
    vga->height = 272;
    vga->bpp = 18;
  } else if (mode == VGA_MODE_1024x768x32) {
    vga->width = 1024;
    vga->height = 768;
    vga->bpp = 32;
  } else {
    log_error("no support mode %x\n");
  }
  vga->mode = mode;
  vga->write = NULL;
  // vga->flip_buffer=gpu_flush;

  vga->framebuffer_index = 0;
  vga->framebuffer_count = 1;
  vga->pframbuffer = 0x27c00000;
  vga->frambuffer = 0xfb000000;

  vga->framebuffer_length = vga->width * vga->height * vga->bpp * 8;

  ssd202_lcd_init(vga);

  return 0;
}
