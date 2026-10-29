#include "bcm2836.h"

#include "kernel/memory.h"
#include "kernel/page.h"
#include "vga/vga.h"

#define RGB24_2_RGB565(r, g, b) \
  (u16)((((r) << 8) & 0xF800) | (((g) << 3) & 0x7E0) | (((b) >> 3)))

static void write_pixel(vga_device_t* vga, uint32_t x, uint32_t y,
                        const pixel_t* pix) {
  u32 color = RGB24_2_RGB565(pix->red, pix->green, pix->blue);
  vga->frambuffer[x + y * vga->width] = color;
}

/* 切换显示平面：写隐藏缓冲后再 SET_VIRT_OFFSET，避免 memcpy 打在可见 FB 上闪屏 */
static void bcm2836_flip_screen(vga_device_t* vga, u32 index) {
  static uint32_t* mailbuffer = NULL;
  u32 y;

  if (vga == NULL || vga->framebuffer_count < 1) {
    return;
  }
  index = index % vga->framebuffer_count;
  y = index * vga->height;

  if (mailbuffer == NULL) {
    mailbuffer = kmalloc_alignment(sizeof(uint32_t) * 8, 16, DEVICE_TYPE);
    if (mailbuffer == NULL) {
      return;
    }
  }

  mailbuffer[0] = 8 * 4;
  mailbuffer[1] = 0;
  mailbuffer[2] = BCM2835_VC_TAG_SET_VIRT_OFFSET;
  mailbuffer[3] = 8;
  mailbuffer[4] = 8;
  mailbuffer[5] = 0;
  mailbuffer[6] = y;
  mailbuffer[7] = 0;

  mailbox_write_read(BCM2835_MAILBOX_PROP_CHANNEL,
                     GPU_MEM_BASE + (uint32_t)(uintptr_t)mailbuffer);
  vga->framebuffer_index = index;
}

int bcm2836_init(vga_device_t* vga) {
  uint32_t* mailbuffer =
      kmalloc_alignment(sizeof(uint32_t) * 28, 16, DEVICE_TYPE);

  if (mailbuffer == NULL) {
    return -1;
  }

  mailbuffer[0] = 22 * 4;
  mailbuffer[1] = 0;

  mailbuffer[2] = BCM2835_VC_TAG_SET_PHYS_WH;
  mailbuffer[3] = 8;
  mailbuffer[4] = 8;
  mailbuffer[5] = vga->width;
  mailbuffer[6] = vga->height;

  /* 虚拟高度 *2：双缓冲，present 时切 Y offset */
  mailbuffer[7] = BCM2835_VC_TAG_SET_VIRT_WH;
  mailbuffer[8] = 8;
  mailbuffer[9] = 8;
  mailbuffer[10] = vga->width;
  mailbuffer[11] = vga->height * 2;

  mailbuffer[12] = BCM2835_VC_TAG_SET_DEPTH;
  mailbuffer[13] = 4;
  mailbuffer[14] = 4;
  mailbuffer[15] = vga->bpp;

  mailbuffer[16] = BCM2835_VC_TAG_SET_PIXEL_ORDER;
  mailbuffer[17] = 4;
  mailbuffer[18] = 4;
  mailbuffer[19] = 0;

  mailbuffer[20] = BCM2835_VC_TAG_ALLOCATE_BUFFER;
  mailbuffer[21] = 8;
  mailbuffer[22] = 8;
  mailbuffer[23] = 16;
  mailbuffer[24] = 0;

  mailbuffer[25] = 0;

  mailbox_write_read(BCM2835_MAILBOX_PROP_CHANNEL,
                     GPU_MEM_BASE + (uint32_t)(uintptr_t)mailbuffer);

  if (mailbuffer[1] != BCM2835_MAILBOX_SUCCESS) {
    log_error("init bcm error code %d\n", mailbuffer[1]);
    return -1;
  }

  u32 fb_addr = mailbuffer[23] & 0x3FFFFFFF;
  u32 fb_size = mailbuffer[24];

  vga->pframbuffer = (u32*)(uintptr_t)fb_addr;
  vga->frambuffer = (u32*)(uintptr_t)0xfb000000UL;
  /* 【如实按字节保存】ALLOCATE_BUFFER 的响应就是分配的字节数。这个值同时
   * 决定页映射长度和 fbdev 上报的 smem_len，必须与真实分配一致。
   * 历史上这里乘过 8（"长度当位数用"的旧习惯），1024x768x32 双缓冲的 6MB
   * 显存被映射/上报成 48MB：对内只是多铺了几十页无主物理内存，但经
   * FBIOGET_FSCREENINFO 外泄给标准 fbdev 程序后，对方会按 48MB 去 mmap。
   * 【兜底】固件没回报字节数（0）时按几何推算，避免一页都不映射。 */
  vga->framebuffer_length =
      (fb_size > 0) ? fb_size
                    : (vga->width * vga->height * ((vga->bpp + 7) / 8) * 2);

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
    log_error("no support mode %x\n", mode);
    return -1;
  }
  vga->mode = mode;
  vga->write = NULL;

  log_debug("map box %x\n", BCM2835_MAILBOX_BASE & ~0xfff);
  page_map(BCM2835_MAILBOX_BASE & ~0xfff, BCM2835_MAILBOX_BASE & ~0xfff,
           PAGE_DEV);

  if (bcm2836_init(vga) < 0) {
    return -1;
  }

  vga->framebuffer_index = 0;
  vga->framebuffer_count = 2;
  vga->flip_buffer = bcm2836_flip_screen;

  u32 addr = (u32)(uintptr_t)vga->frambuffer;
  log_debug("fb addr:%x end:%x len:%x\n", addr,
            addr + vga->framebuffer_length, vga->framebuffer_length);
  if (addr == 0) {
    return -1;
  }

  log_debug("map fb start %x %x\n", addr, (u32)(uintptr_t)vga->pframbuffer);
  vmemory_map_phys((u32)(uintptr_t)vga->frambuffer,
                   (u32)(uintptr_t)vga->pframbuffer, vga->framebuffer_length,
                   MEMORY_FB, PAGE_FB);

  /* 初始显示平面 0 */
  bcm2836_flip_screen(vga, 0);
  return 0;
}
