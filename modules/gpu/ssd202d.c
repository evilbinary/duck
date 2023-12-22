/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "gpu.h"
#include "kernel/kernel.h"
#include "vga/vga.h"

#define USE_COPY_BUFFER 1

static inline uint8_t rgb2y(uint8_t *rgb) {
  // return(((257 * rgb[0])/1000) + ((504 * rgb[1])/1000) + ((98 * rgb[2])/1000)
  // + 16);
  uint32_t b = rgb[0];
  uint32_t g = rgb[1];
  uint32_t r = rgb[2];
  return (306 * r + 601 * g + 117 * b) >> 10;
}

static inline void rgb2uv(uint8_t *rgb, uint8_t *uv) {
  uint32_t b = rgb[0];
  uint32_t g = rgb[1];
  uint32_t r = rgb[2];
  uv[0] = ((446 * b - 150 * r - 296 * g) / 1024) + 0x7F;
  uv[1] = ((630 * r - 527 * g - 102 * b) / 1024) + 0x7F;
}

int rgb2nv12(uint8_t *out, uint32_t *in, int w, int h) {
  uint8_t *y = out;
  uint8_t *uv = y + w * h;
  uint32_t *rgb = (in + w * h);

  for (int i = 0; i < w; i++) {
    for (int j = 0; j < h; j++) {
      *y = rgb2y((uint8_t *)rgb);
      y++;
      rgb--;
    }
  }

  return 0;
}

void ssd202d_flip_screen(vga_device_t *vga, u32 index) {
  // vga->framebuffer_index = index;
  // kprintf("flip %d %d %d\n",index,vga->width,vga->height);
  rgb2nv12(vga->pframbuffer, vga->frambuffer, vga->width, vga->height);
}

int ssd202_lcd_init(vga_device_t *vga) {
  u32 addr = vga->frambuffer;
  u32 paddr = vga->pframbuffer;

#ifdef USE_COPY_BUFFER
  vga->bpp = 24;
  vga->flip_buffer = ssd202d_flip_screen;
  paddr =kmalloc(vga->framebuffer_length, DEVICE_TYPE);
  addr = vga->frambuffer ;
  
  for (int i = 0; i < vga->framebuffer_length / PAGE_SIZE; i++) {
    page_map(addr, paddr, PAGE_USR);
    addr += 0x1000;
    paddr += 0x1000;
  }
  addr = vga->pframbuffer;
  paddr = vga->pframbuffer;
#endif

  for (int i = 0; i < vga->framebuffer_length / PAGE_SIZE; i++) {
    page_map(addr, paddr, PAGE_DEV);
    addr += 0x1000;
    paddr += 0x1000;
  }

  log_info("fb addr:%x end:%x len:%x\n", vga->frambuffer,
           vga->frambuffer + vga->framebuffer_length, vga->framebuffer_length);

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

  // 默认配置
  vga->width = 640;
  vga->height = 480;
  vga->bpp = 16;

  vga->framebuffer_index = 0;
  vga->framebuffer_count = 1;
  vga->pframbuffer = 0x27c00000;
  vga->frambuffer = 0xfb000000;

  vga->framebuffer_length = vga->width * vga->height * vga->bpp/2;

  ssd202_lcd_init(vga);

  return 0;
}
