/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "pl110.h"
#include "gpu.h"
#include "vga/vga.h"
#include "kernel/memory.h"

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
  /* 显存在 RAM 顶部 2MB 内（0x07e00000，跨度 640*480*4 = 0x12C000）。
   * **这个地址必须与 boot/arm/init-armv5.c 的 VERSATILEPB 内存条目一致**：
   * 那边把 RAM 顶部 2MB 从 type==1 条目里排除掉（`length = 0x07e00000-0x10000`），
   * 这样页分配器根本看不到显存段，不会把显存页发给用户 mmap（否则用户页与显示
   * 缓冲同物理页，GPU 每帧刷黑 ⇒ 用户结构体被写成 0xff000000）。
   * 改这里的地址就要同步改那边，反之亦然。
   * 历史：原值是 0x00fb0000（16.4MB）——落在内核管理的 RAM 中部，且没人排除，
   * 于是被页分配器发出去，成为 PopupManager 崩溃的根因（那个值也像是从 raspi2
   * 的 0xFB000000 抄漏了两位）。 */
  vga->pframbuffer = 0x08000000 - 0x200000;
  vga->frambuffer = vga->pframbuffer;

  vga->format = VGA_FMT_ARGB8888;

  /* 【修复】fb 长度按字节数（w*h*4）：原来按像素数，导致下面的 fb 映射
   * 循环只映射 1/4 显存（75 页 vs 300 页），屏幕下方 3/4 的写操作落在
   * 未映射/被其它用途占用的内存上。 */
  vga->framebuffer_length = vga->width * vga->height * 4;

  vga->flip_buffer = NULL;
  pl110_lcd_init(vga);

  /* 按字节算 end：`vga->frambuffer + vga->framebuffer_length` 是指针算术
   * （frambuffer 是 u32*），会把长度乘 4，打出来的 end 比真实值大 3 倍多。 */
  log_info("fb addr:%x end:%x len:%x\n", (u32)(uintptr_t)vga->frambuffer,
           (u32)(uintptr_t)vga->frambuffer + vga->framebuffer_length,
           vga->framebuffer_length);

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

  page_map(VERSATILEPB_OSC1, VERSATILEPB_OSC1, PAGE_DEV);

  page_map(VERSATILEPB_PL110_LCD_BASE, VERSATILEPB_PL110_LCD_BASE, PAGE_DEV);

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
  *(volatile u32 *)(VERSATILEPB_PL110_LCD_BASE + LCD_IMSC) = 0x92B;

  log_debug("lcd %dx%d len= %d\n", lcd->width, lcd->height,
            vga->framebuffer_length);

  // map fb：内核页表映射 + 登记 fb VMA（进程缺页时按 PAGE_FB 镜像）
  vmemory_map_phys((u32)(uintptr_t)vga->frambuffer,
                   (u32)(uintptr_t)vga->pframbuffer, vga->framebuffer_length,
                   MEMORY_FB, PAGE_FB);
  log_debug("map fb %x %x len %x\n", vga->frambuffer, vga->pframbuffer,
            vga->framebuffer_length);

  log_info("pl110_lcd_init end\n");

  return 0;
}
