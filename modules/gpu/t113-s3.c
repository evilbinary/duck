/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "t113-s3.h"

#include "t113-ccu.h"
#include "t113-de.h"
#include "t113-tcon.h"
#include "vga/vga.h"
#include "gpio/sunxi-gpio.h"

static void inline t113_de_enable(t113_s3_lcd_t *pdat) {
  struct de_glb_t *glb = (struct de_glb_t *)(pdat->de + T113_DE_MUX_GLB);
  io_write32((u32)&glb->dbuff, 1);
}

static inline void t113_de_set_address(t113_s3_lcd_t *pdat, void *vram) {
  struct de_ui_t *ui =
      (struct de_ui_t *)(pdat->de + T113_DE_MUX_CHAN + 0x1000 * 1);
  io_write32((u32)&ui->cfg[0].top_laddr, (u32)(unsigned long)vram);
}

static inline void t113_de_set_mode(t113_s3_lcd_t *pdat) {
  struct de_clk_t *clk = (struct de_clk_t *)(pdat->de);
  struct de_glb_t *glb = (struct de_glb_t *)(pdat->de + T113_DE_MUX_GLB);
  struct de_bld_t *bld = (struct de_bld_t *)(pdat->de + T113_DE_MUX_BLD);
  struct de_ui_t *ui =
      (struct de_ui_t *)(pdat->de + T113_DE_MUX_CHAN + 0x1000 * 1);
  u32 size = (((pdat->height - 1) << 16) | (pdat->width - 1));
  u32 val;
  int i;

  val = io_read32((u32)&clk->rst_cfg);
  val |= 1 << 0;
  io_write32((u32)&clk->rst_cfg, val);

  val = io_read32((u32)&clk->gate_cfg);
  val |= 1 << 0;
  io_write32((u32)&clk->gate_cfg, val);

  val = io_read32((u32)&clk->bus_cfg);
  val |= 1 << 0;
  io_write32((u32)&clk->bus_cfg, val);

  val = io_read32((u32)&clk->sel_cfg);
  val &= ~(1 << 0);
  io_write32((u32)&clk->sel_cfg, val);

  io_write32((u32)&glb->ctl, (1 << 0));
  io_write32((u32)&glb->status, 0);
  io_write32((u32)&glb->dbuff, 1);
  io_write32((u32)&glb->size, size);

  for (i = 0; i < 4; i++) {
    void *chan = (void *)(pdat->de + T113_DE_MUX_CHAN + 0x1000 * i);
    memset(chan, 0, i == 0 ? sizeof(struct de_vi_t) : sizeof(struct de_ui_t));
  }
  memset(bld, 0, sizeof(struct de_bld_t));

  io_write32((u32)&bld->fcolor_ctl, 0x00000101);
  io_write32((u32)&bld->route, 1);
  io_write32((u32)&bld->premultiply, 0);
  io_write32((u32)&bld->bkcolor, 0xff000000);
  io_write32((u32)&bld->bld_mode[0], 0x03010301);
  io_write32((u32)&bld->bld_mode[1], 0x03010301);
  io_write32((u32)&bld->output_size, size);
  io_write32((u32)&bld->out_ctl, 0);
  io_write32((u32)&bld->ck_ctl, 0);
  for (i = 0; i < 4; i++) {
    io_write32((u32)&bld->attr[i].fcolor, 0xff000000);
    io_write32((u32)&bld->attr[i].insize, size);
  }

  io_write32(pdat->de + T113_DE_MUX_VSU, 0);
  io_write32(pdat->de + T113_DE_MUX_GSU1, 0);
  io_write32(pdat->de + T113_DE_MUX_GSU2, 0);
  io_write32(pdat->de + T113_DE_MUX_GSU3, 0);
  io_write32(pdat->de + T113_DE_MUX_FCE, 0);
  io_write32(pdat->de + T113_DE_MUX_BWS, 0);
  io_write32(pdat->de + T113_DE_MUX_LTI, 0);
  io_write32(pdat->de + T113_DE_MUX_PEAK, 0);
  io_write32(pdat->de + T113_DE_MUX_ASE, 0);
  io_write32(pdat->de + T113_DE_MUX_FCC, 0);
  io_write32(pdat->de + T113_DE_MUX_DCSC, 0);

  io_write32((u32)&ui->cfg[0].attr, (1 << 0) | (4 << 8) | (1 << 1) | (0xff << 24));
  io_write32((u32)&ui->cfg[0].size, size);
  io_write32((u32)&ui->cfg[0].coord, 0);
  io_write32((u32)&ui->cfg[0].pitch, 4 * pdat->width);
  io_write32((u32)&ui->cfg[0].top_laddr,
          (u32)(unsigned long)pdat->vram[pdat->index]);
  io_write32((u32)&ui->ovl_size, size);
}

static void t113_tconlcd_enable(t113_s3_lcd_t *pdat) {
  struct t113_tconlcd_reg_t *tcon = (struct t113_tconlcd_reg_t *)pdat->tcon;
  u32 val;

  val = io_read32((u32)&tcon->gctrl);
  val |= (1 << 31);
  io_write32((u32)&tcon->gctrl, val);
}

static void t113_tconlcd_disable(t113_s3_lcd_t *pdat) {
  struct t113_tconlcd_reg_t *tcon = (struct t113_tconlcd_reg_t *)pdat->tcon;
  u32 val;

  val = io_read32((u32)&tcon->dclk);
  val &= ~(0xf << 28);
  io_write32((u32)&tcon->dclk, val);

  io_write32((u32)&tcon->gctrl, 0);
  io_write32((u32)&tcon->gint0, 0);
}

static void t113_tconlcd_set_timing(t113_s3_lcd_t *pdat) {
  struct t113_tconlcd_reg_t *tcon = (struct t113_tconlcd_reg_t *)pdat->tcon;
  int bp, total;
  u32 val;

  val = (pdat->timing.v_front_porch + pdat->timing.v_back_porch +
         pdat->timing.v_sync_len) /
        2;
  io_write32((u32)&tcon->ctrl,
          (1 << 31) | (0 << 24) | (0 << 23) | ((val & 0x1f) << 4) | (0 << 0));
  val = clk_get_rate(pdat->clk_tconlcd) / pdat->timing.pixel_clock_hz;
  io_write32((u32)&tcon->dclk, (0xf << 28) | (val << 0));
  io_write32((u32)&tcon->timing0,
          ((pdat->width - 1) << 16) | ((pdat->height - 1) << 0));
  bp = pdat->timing.h_sync_len + pdat->timing.h_back_porch;
  total = pdat->width + pdat->timing.h_front_porch + bp;
  io_write32((u32)&tcon->timing1, ((total - 1) << 16) | ((bp - 1) << 0));
  bp = pdat->timing.v_sync_len + pdat->timing.v_back_porch;
  total = pdat->height + pdat->timing.v_front_porch + bp;
  io_write32((u32)&tcon->timing2, ((total * 2) << 16) | ((bp - 1) << 0));
  io_write32((u32)&tcon->timing3, ((pdat->timing.h_sync_len - 1) << 16) |
                                   ((pdat->timing.v_sync_len - 1) << 0));

  val = (0 << 31) | (1 << 28);
  if (!pdat->timing.h_sync_active) val |= (1 << 25);
  if (!pdat->timing.v_sync_active) val |= (1 << 24);
  if (!pdat->timing.den_active) val |= (1 << 27);
  if (!pdat->timing.clk_active) val |= (1 << 26);
  io_write32((u32)&tcon->io_polarity, val);
  io_write32((u32)&tcon->io_tristate, 0);
}

static void t113_tconlcd_set_dither(t113_s3_lcd_t *pdat) {
  struct t113_tconlcd_reg_t *tcon = (struct t113_tconlcd_reg_t *)pdat->tcon;

  if ((pdat->bits_per_pixel == 16) || (pdat->bits_per_pixel == 18)) {
    io_write32((u32)&tcon->frm_seed[0], 0x11111111);
    io_write32((u32)&tcon->frm_seed[1], 0x11111111);
    io_write32((u32)&tcon->frm_seed[2], 0x11111111);
    io_write32((u32)&tcon->frm_seed[3], 0x11111111);
    io_write32((u32)&tcon->frm_seed[4], 0x11111111);
    io_write32((u32)&tcon->frm_seed[5], 0x11111111);
    io_write32((u32)&tcon->frm_table[0], 0x01010000);
    io_write32((u32)&tcon->frm_table[1], 0x15151111);
    io_write32((u32)&tcon->frm_table[2], 0x57575555);
    io_write32((u32)&tcon->frm_table[3], 0x7f7f7777);

    if (pdat->bits_per_pixel == 16)
      io_write32((u32)&tcon->frm_ctrl, (1 << 31) | (1 << 6) | (0 << 5) | (1 << 4));
    else if (pdat->bits_per_pixel == 18)
      io_write32((u32)&tcon->frm_ctrl, (1 << 31) | (0 << 6) | (0 << 5) | (0 << 4));
  }
}

static void fb_t113_cfg_gpios(int gpio, int pin, int n, int cfg, int pull,
                              int drv) {
  for (; n > 0; n--, gpio++) {
    // todo
    gpio_config(gpio, pin + n, cfg);
    gpio_pull(gpio, pin + n, pull);
    // gpio_output()
    //  gpio_set_drv(base, drv);
  }
}

int clk_get_rate(const char *name) {
  int val = 0;
  // todo

  return val;
}

// static void fb_setbl(vga_device_t *fb, int brightness) {
//   t113_s3_lcd_t *pdat = (t113_s3_lcd_t *)fb->priv;
//   led_set_brightness(pdat->backlight, brightness);
// }

// static int fb_getbl(vga_device_t *fb) {
//   t113_s3_lcd_t *pdat = (t113_s3_lcd_t *)fb->priv;
//   return led_get_brightness(pdat->backlight);
// }

static void fb_present(vga_device_t *fb, struct surface_t *s,
                       struct region_list_t *rl) {
  t113_s3_lcd_t *pdat = (t113_s3_lcd_t *)fb->priv;

  // todo
  //  memcpy(pdat->vram[pdat->index], s->pixels, s->pixlen);
  //  dma_cache_sync(pdat->vram[pdat->index], pdat->pixlen, DMA_TO_DEVICE);
  t113_de_set_address(pdat, pdat->vram[pdat->index]);
  t113_de_enable(pdat);
}

int gpu_init_mode(vga_device_t *vga, int mode) {
  log_info("t113_lcd_init\n");
  t113_s3_lcd_t *lcd = kmalloc(sizeof(t113_s3_lcd_t), DEFAULT_TYPE);
  vga->priv = lcd;

  lcd->de = T113_DE_BASE;
  lcd->tcon = T113_TCON_BASE;

  // lcd->rstde = 44;
  // lcd->rsttcon = 36;
  lcd->width = vga->width;
  lcd->height = vga->height;
  lcd->bits_per_pixel = 18;
  lcd->bytes_per_pixel = 4;
  lcd->index = 0;
  lcd->vram[0] = vga->pframbuffer;
  lcd->vram[1] = vga->pframbuffer;
  vga->framebuffer_length = lcd->width * lcd->height * lcd->bytes_per_pixel * 8;

  lcd->timing.pixel_clock_hz = 33000000;
  lcd->timing.h_front_porch = 40;
  lcd->timing.h_back_porch = 87;
  lcd->timing.h_sync_len = 1;
  lcd->timing.v_front_porch = 13;
  lcd->timing.v_back_porch = 31;
  lcd->timing.v_sync_len = 1;
  lcd->timing.h_sync_active = 0;
  lcd->timing.v_sync_active = 0;
  lcd->timing.den_active = 1;
  lcd->timing.clk_active = 1;

  // map tcon 4k
  page_map(T113_TCON_BASE, T113_TCON_BASE, 0);
  // map ccu 1k
  page_map(T113_CCU_BASE, T113_CCU_BASE, 0);

  // map de 2m
  u32 addr = T113_DE_BASE;
  for (int i = 0; i < 1024 * 1024 * 2 / PAGE_SIZE; i++) {
    page_map(addr, addr, 0);
    addr += 0x1000;
  }

  // vga->pframbuffer=kmalloc(vga->framebuffer_length*2,DEFAULT_TYPE);
  // map fb
  addr = vga->frambuffer;
  u32 paddr = vga->pframbuffer;
  for (int i = 0; i < vga->framebuffer_length / PAGE_SIZE; i++) {
    page_map(addr, paddr, PAGE_DEV);
    addr += 0x1000;
    paddr += 0x1000;
  }

  // init
  // todo clk video enable
  // clk_enable(pdat->clk_de);
  // clk_enable(pdat->clk_tconlcd);

  // todo gpio set

  if (lcd->bits_per_pixel == 16) {
    //   fb_t113_cfg_gpios(T113_GPIOD1, 5, 0x2, GPIO_PULL_NONE,
    //   GPIO_DRV_STRONG); fb_t113_cfg_gpios(T113_GPIOD6, 6, 0x2,
    //   GPIO_PULL_NONE, GPIO_DRV_STRONG); fb_t113_cfg_gpios(T113_GPIOD13, 5,
    //   0x2, GPIO_PULL_NONE, GPIO_DRV_STRONG); fb_t113_cfg_gpios(T113_GPIOD18,
    //   4, 0x2, GPIO_PULL_NONE, GPIO_DRV_STRONG);
  } else if (lcd->bits_per_pixel == 18) {
    fb_t113_cfg_gpios(GPIO_D, 0, 6, 0x2, GPIO_PULL_UP, 0);
    fb_t113_cfg_gpios(GPIO_D, 6, 6, 0x2, GPIO_PULL_UP, 0);
    fb_t113_cfg_gpios(GPIO_D, 12, 6, 0x2, GPIO_PULL_UP, 0);
    fb_t113_cfg_gpios(GPIO_D, 18, 4, 0x2, GPIO_PULL_UP, 0);
  }

  // tcon init
  t113_tconlcd_disable(lcd);
  t113_tconlcd_set_timing(lcd);
  t113_tconlcd_set_dither(lcd);
  t113_tconlcd_enable(lcd);
  t113_de_set_mode(lcd);
  t113_de_enable(lcd);
  t113_de_set_address(lcd, lcd->vram[lcd->index]);
  t113_de_enable(lcd);

  log_info("t113_lcd_init end\n");

  return 0;
}