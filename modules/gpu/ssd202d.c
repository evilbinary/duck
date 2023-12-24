/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "gpu.h"
#include "kernel/kernel.h"
#include "vga/vga.h"

// #define USE_COPY_BUFFER 1

void ssd202d_flip_screen(vga_device_t *vga, u32 index) {
  // vga->framebuffer_index = index;
  // kprintf("flip %d %d %d\n",index,vga->width,vga->height);
  // rgb2nv12(vga->pframbuffer, vga->frambuffer, vga->width, vga->height);
  kmemcpy(vga->pframbuffer, vga->frambuffer, vga->width * vga->height);
}

int ssd202_lcd_init(vga_device_t *vga) {
  u32 addr = vga->frambuffer;
  u32 paddr = vga->pframbuffer;

#ifdef USE_COPY_BUFFER
  vga->bpp = 24;
  vga->flip_buffer = ssd202d_flip_screen;
  paddr = kmalloc(vga->framebuffer_length, DEVICE_TYPE);
  addr = vga->frambuffer;

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



void test_fb(int count) {
  int ysta = 0;
  int xsta = 0;
  int yend = 480;
  int xend = 640;
  u32 color=0xff0000;
  u8 fb = 0xfb000000;
  u8 pfb = 0x27c00000;

  u8 *p = pfb;
  for (int i = ysta; i < yend; i++) {
    for (int j = xsta; j < xend; j++) {
      *p++ = color;
    }
  }

  // rgb2nv12(pfb, fb, xend, yend);

  log_debug("fps %d\n", count);
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

  vga->format = 1;  // nv12

  vga->framebuffer_length = vga->width * vga->height * vga->bpp / 2;

  ssd202_lcd_init(vga);

  // for(int i=0;;i++){
  //   test_fb(i);
  // }

  return 0;
}
