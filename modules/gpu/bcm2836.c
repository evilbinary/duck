#include "bcm2836.h"

#include "vga/vga.h"

#define RGB24_2_RGB565(r, g, b) \
  (u16)((((r) << 8) & 0xF800) | (((g) << 3) & 0x7E0) | (((b) >> 3)))

static void write_pixel(vga_device_t* vga, uint32_t x, uint32_t y,
                        const pixel_t* pix) {
  u32 color = RGB24_2_RGB565(pix->red, pix->green, pix->blue);
  vga->frambuffer[x + y * vga->width] = color;
}

int bcm2836_init(vga_device_t* vga) {
  // uint32_t __attribute__((aligned(16))) mailbuffer[28];
  uint32_t* mailbuffer =
      kmalloc_alignment(sizeof(uint32_t) * 28, 16, DEVICE_TYPE);

  int i = 0;
  mailbuffer[0] = 22 * 4;
  mailbuffer[1] = 0;

  mailbuffer[2] = BCM2835_VC_TAG_SET_PHYS_WH;
  mailbuffer[3] = 8;
  mailbuffer[4] = 8;
  mailbuffer[5] = vga->width;
  mailbuffer[6] = vga->height;

  mailbuffer[7] = BCM2835_VC_TAG_SET_VIRT_WH;
  mailbuffer[8] = 8;
  mailbuffer[9] = 8;
  mailbuffer[10] = vga->width;
  mailbuffer[11] = vga->height;

  mailbuffer[12] = BCM2835_VC_TAG_SET_DEPTH;
  mailbuffer[13] = 4;
  mailbuffer[14] = 4;
  mailbuffer[15] = vga->bpp;  // depth

  // set pix order
  mailbuffer[16] = BCM2835_VC_TAG_SET_PIXEL_ORDER;
  mailbuffer[17] = 4;
  mailbuffer[18] = 4;
  mailbuffer[19] = 0;  // 0x0: BGR 0x1: RGB

  mailbuffer[20] = BCM2835_VC_TAG_ALLOCATE_BUFFER;
  mailbuffer[21] = 8;
  mailbuffer[22] = 8;
  mailbuffer[23] = 16;  // pointer
  mailbuffer[24] = 0;

  mailbuffer[25] = 0;

  mailbox_write_read(BCM2835_MAILBOX_PROP_CHANNEL, mailbuffer);
  // mailbox_write_read(BCM2835_MAILBOX_FB_CHANNEL, mailbuffer);

  if (mailbuffer[1] != BCM2835_MAILBOX_SUCCESS) {
    log_error("init bcm error code %d\n", mailbuffer[1]);
    return -1;
  }

  u32 fb_addr = mailbuffer[23] & 0x3FFFFFFF;
  u32 fb_size = mailbuffer[24];

  vga->pframbuffer = fb_addr;
  vga->frambuffer = 0xfb000000;

  vga->framebuffer_length = fb_size*8;

  return 0;
}

int gpu_init_mode(vga_device_t* vga, int mode) {
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
  } else if (mode == VGA_MODE_640x480x32) {
    vga->width = 640;
    vga->height = 480;
    vga->bpp = 32;
  }  else if (mode == VGA_MODE_480x272x24) {
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

  log_debug("map box %x\n", BCM2835_MAILBOX_BASE & ~0xfff);
  page_map(BCM2835_MAILBOX_BASE & ~0xfff, BCM2835_MAILBOX_BASE & ~0xfff,
           PAGE_DEV);

  bcm2836_init(vga);

  vga->framebuffer_index = 0;
  vga->framebuffer_count = 1;
  log_debug("fb addr:%x end:%x len:%x\n", vga->frambuffer,
            vga->frambuffer + vga->framebuffer_length, vga->framebuffer_length);
  u32 addr = vga->frambuffer;
  if (addr <= 0) {
    return;
  }

  u32 paddr = vga->pframbuffer;
  log_debug("map fb start %x %x\n", addr, paddr);

  for (int i = 0; i < vga->framebuffer_length / PAGE_SIZE; i++) {
    page_map(addr, paddr, PAGE_DEV);
    addr += PAGE_SIZE;
    paddr += PAGE_SIZE;
  }
  log_debug("map fb end %x %x\n", addr, paddr);

  return 0;
}