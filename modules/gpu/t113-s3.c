/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "t113-s3.h"

#include "gpio.h"
#include "gpio/sunxi-gpio.h"
#include "t113-ccu.h"
#include "t113-de.h"
#include "t113-tcon.h"
#include "vga/vga.h"

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

  io_write32((u32)&ui->cfg[0].attr,
             (1 << 0) | (4 << 8) | (1 << 1) | (0xff << 24));
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
  // LCD_GCTL_REG
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

  u32 start_delay = pdat->timing.vt - pdat->timing.y - 10;

  if (start_delay < 10)
    start_delay = 10;
  else if (start_delay > 31)
    start_delay = 31;

  kprintf("start_delay %d\n", start_delay);

  // LCD_CTL_REG LCD_EN | HV(Sync+DE)  | Default ｜001: Color Check   111:
  // Gridding Check
  io_write32((u32)&tcon->ctrl, (1 << 31) | (0 << 24) | (0 << 23) | 1 << 20 |
                                   ((start_delay & 0x1f) << 4) | (0 << 0));

  val = pdat->clk_tconlcd / pdat->timing.pixel_clock_hz;
  // LCD_DCLK_REG LCD_DCLK_EN 0xf LCD_DCLK_DIV  <6
  kprintf("dclk %x\n", val);
  if (val > 0) {
    io_write32((u32)&tcon->dclk, (0xf << 28) | (val << 0));
  }

  // LCD_BASIC0_REG
  io_write32((u32)&tcon->timing0,
             ((pdat->timing.x - 1) << 16) | ((pdat->timing.y - 1) << 0));

  u32 vt = pdat->timing.vt * 2;

  u32 ht = pdat->timing.ht - 1;

  u32 hbp = 0;
  if (pdat->timing.hbp > 1) {
    hbp = pdat->timing.hbp - 1;
  }

  u32 vbp = 0;
  if (pdat->timing.vbp > 1) {
    vbp = pdat->timing.vbp - 1;
  }
  u32 hspw = 0;
  if (pdat->timing.hspw > 1) {
    hspw = pdat->timing.hspw - 1;
  }

  u32 vspw = 0;
  if (pdat->timing.vspw > 1) {
    vspw = pdat->timing.vspw - 1;
  }

  // LCD_BASIC1_REG
  io_write32((u32)&tcon->timing1, (ht << 16) | hbp << 0);

  // LCD_BASIC2_REG

  io_write32((u32)&tcon->timing2, (vt << 16) | vbp << 0);

  // LCD_BASIC3_REG
  io_write32((u32)&tcon->timing3, (hspw << 16) | vspw << 0);

  kprintf("ht %d vt %d\n", ht, vt);

  val = (0 << 31) | (1 << 28);
  if (!pdat->timing.h_sync_active) val |= (1 << 25);
  if (!pdat->timing.v_sync_active) val |= (1 << 24);
  if (!pdat->timing.den_active) val |= (1 << 27);
  if (!pdat->timing.clk_active) val |= (1 << 26);

  io_write32((u32)&tcon->io_polarity, val);

  // LCD_IO_TRI_REG 0 start
  io_write32((u32)&tcon->io_tristate, 0);

  // // LCD_IO_POL_REG  IO_OUTPUT_SEL
  // io_write32((u32)&tcon->io_tristate, 1 << 31);

  // LCD_HV_IF_REG  1000: 8-bit/3-cycle RGB serial mode (RGB888)
  //  io_write32((u32)&tcon->cpu_intf, 8 << 28);
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
      // 5-6-6
      io_write32((u32)&tcon->frm_ctrl,
                 (1 << 31) | (1 << 6) | (0 << 5) | (1 << 4));
    else if (pdat->bits_per_pixel == 18)
      // 6-6-6
      io_write32((u32)&tcon->frm_ctrl,
                 (1 << 31) | (0 << 6) | (0 << 5) | (0 << 4));
  }
}

static void fb_t113_cfg_gpios(int gpio, int pin, int n, int cfg, int pull,
                              int drv) {
  for (int i = 0; i < n; i++) {
    gpio_config(gpio, pin + i, cfg);
    gpio_pull(gpio, pin + i, pull);
    gpio_drive(gpio, pin + i, drv);
  }
}

void t113_flush_screen(vga_device_t *vga, u32 index) {
  // vga->framebuffer_index = index;
  // kprintf("flip %d %d %d\n",index,vga->width,vga->height);
  // rgb2nv12(vga->pframbuffer, vga->frambuffer, vga->width, vga->height);
  // kmemcpy(vga->pframbuffer, vga->frambuffer, vga->width * vga->height);
  cpu_invalid_tlb();
  cpu_cache_flush_range(vga->pframbuffer, vga->width * vga->height);
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

  // vga->width = 320;
  // vga->height = 480;

  vga->width = 480;
  vga->height = 320;

  vga->framebuffer_index = 0;
  vga->framebuffer_count = 1;
  vga->pframbuffer = 0x73e00000;
  vga->frambuffer = 0xfb000000;

  vga->flip_buffer = t113_flush_screen;
  t113_lcd_init(vga);

  log_info("fb addr:%x end:%x len:%x\n", vga->frambuffer,
           vga->frambuffer + vga->framebuffer_length, vga->framebuffer_length);

  u32 *buffer = vga->frambuffer;
  for (int i = 0; i < vga->framebuffer_length / 4; i++) {
    buffer[i] = 0x000000;
  }
}

int t113_lcd_init(vga_device_t *vga) {
  log_info("t113_lcd_init\n");
  t113_s3_lcd_t *lcd = kmalloc(sizeof(t113_s3_lcd_t), DEFAULT_TYPE);
  vga->priv = lcd;

  lcd->de = T113_DE_BASE;
  lcd->tcon = T113_TCON_BASE;

  lcd->width = vga->width;
  lcd->height = vga->height;
  lcd->bits_per_pixel = 18;
  lcd->bytes_per_pixel = 4;
  lcd->index = 0;

  vga->framebuffer_length = lcd->width * lcd->height * lcd->bytes_per_pixel * 8;

  log_debug("lcd %dx%d len= %d\n", lcd->width, lcd->height,
            vga->framebuffer_length);

  lcd->timing.pixel_clock_hz = 19000000;

  lcd->timing.h_sync_active = 0;
  lcd->timing.v_sync_active = 0;
  lcd->timing.den_active = 1;
  lcd->timing.clk_active = 1;

  lcd->timing.x = vga->width;
  lcd->timing.y = vga->height;

  // lcd->timing.vt = 486;
  // lcd->timing.vbp = 2;
  // lcd->timing.vfp = 10;
  // lcd->timing.vspw = 1;

  // lcd->timing.ht = 342;
  // lcd->timing.hbp = 20;
  // lcd->timing.hfp = 10;
  // lcd->timing.hspw = 2;

  lcd->timing.ht = 490 + 8;
  lcd->timing.hbp = 2;
  lcd->timing.hfp = 0;
  lcd->timing.hspw = 2;

  lcd->timing.vt = 342;
  lcd->timing.vbp = 20;
  lcd->timing.vfp = 0;
  lcd->timing.vspw = 2;

  int h = lcd->timing.x + lcd->timing.hfp + lcd->timing.hbp + lcd->timing.hspw;
  int w = lcd->timing.y + lcd->timing.vfp + lcd->timing.vbp + lcd->timing.vspw;

  lcd->timing.pixel_clock_hz = h * w * 60;
  // lcd->timing.pixel_clock_hz = 10000000;
  log_debug("pixel_clock_hz %d\n", lcd->timing.pixel_clock_hz);

  // vga->pframbuffer=kmalloc(vga->framebuffer_length*2,DEFAULT_TYPE);

  lcd->vram[0] = vga->frambuffer;
  lcd->vram[1] = vga->pframbuffer;

  // map fb
  u32 addr = vga->frambuffer;
  u32 paddr = vga->pframbuffer;
  for (int i = 0; i < vga->framebuffer_length / PAGE_SIZE; i++) {
    page_map(addr, paddr, PAGE_DEV);
    addr += 0x1000;
    paddr += 0x1000;
  }

  // map tcon 4k
  page_map(T113_TCON_BASE, T113_TCON_BASE, 0);

  // map ccu 1k
  page_map(T113_CCU_BASE, T113_CCU_BASE, 0);

  // map de 2m
  page_map(T113_DE_BASE, T113_DE_BASE, 0);

  addr = T113_DE_BASE + T113_DE_MUX_GLB;
  for (int i = 0; i < 1024 * 1024 * 2 * 2 / PAGE_SIZE; i++) {
    page_map(addr, addr, 0);
    addr += 0x1000;
  }

  // init
  // clk video enable
  // PLL_VIDEO0  gate enable
  u32 val = io_read32(T113_CCU_BASE + CCU_PLL_VIDEO0_CTRL_REG);
  // val |= 1 << 24;  // PLL_SDM_EN
  val |= 1 << 27;  // PLL_OUTPUT_GATE
  // val |= 1 << 30;  // PLL_LDO_EN
  val |= 1 << 31;  // PLL_EN

  // // 24*9/ 1/4 = PLL_VIDEO0(1X)=24MHz*N/M/4
  val &= ~(0xff << 8);
  val |= 20 << 8;  // PLL_N

  val &= ~(2 << 0);
  val |= 0 << 0;  // PLL_M

  io_write32(T113_CCU_BASE + CCU_PLL_VIDEO0_CTRL_REG, val);

  u32 video_rate = sunxi_clk_get_video_rate();
  lcd->clk_tconlcd = video_rate;

  // lcd->clk_tconlcd = sunxi_clk_get_peri1x_rate();
  // lcd->clk_tconlcd= 297 000 000;
  log_debug("video0 rate %d lcd clk %d\n", video_rate, lcd->clk_tconlcd);

  // enable de clk   000: PLL_PERI(2X) 001: PLL_VIDEO0(4X) DE_CLK = Clock
  // Source/M.
  io_write32(T113_CCU_BASE + 0x0600, 1 << 31 | 1 << 24 | 0);

  // reset de 16 DE Bus Gating Reset Register
  io_write32(T113_CCU_BASE + 0x060C, 0);
  io_write32(T113_CCU_BASE + 0x060C, 1 << 16);

  val = io_read32(T113_CCU_BASE + 0x060C);
  io_write32(T113_CCU_BASE + 0x060C, val | 1 << 0);

  // TCON LCD0 Clock register 000: PLL_VIDEO0(1X) 297/4/7
  val = 0;
  val |= 1 << 31;
  val |= 0 << 24;  // 000: PLL_VIDEO0(1X)
  val |= 0 << 8;   // FACTOR_N 00: 1 01: 2 10: 4
  val |= 0 << 0;   // M= FACTOR_M +1.
  io_write32(T113_CCU_BASE + 0xB60, val);

  // reset tconlcd 912 TCONLCD Bus Gating Reset Register
  // io_write32(T113_CCU_BASE + 0x0B7C, 0);

  io_write32(T113_CCU_BASE + 0x0B7C, 1 << 16);
  val = io_read32(T113_CCU_BASE + 0x0B7C);
  io_write32(T113_CCU_BASE + 0x0B7C, 1 << 0 | val);

  // gpio set
  if (lcd->bits_per_pixel == 16) {
    fb_t113_cfg_gpios(GPIO_D, 1, 5, 0x2, GPIO_PULL_DISABLE, 3);
    fb_t113_cfg_gpios(GPIO_D, 6, 6, 0x2, GPIO_PULL_DISABLE, 3);
    fb_t113_cfg_gpios(GPIO_D, 13, 5, 0x2, GPIO_PULL_DISABLE, 3);
    fb_t113_cfg_gpios(GPIO_D, 18, 4, 0x2, GPIO_PULL_DISABLE, 3);

  } else if (lcd->bits_per_pixel == 18) {
    // pd0-pd21
    log_info("bits_per_pixel %d\n", lcd->bits_per_pixel);

    fb_t113_cfg_gpios(GPIO_D, 0, 6, 0x2, GPIO_PULL_DISABLE, 3);
    fb_t113_cfg_gpios(GPIO_D, 6, 6, 0x2, GPIO_PULL_DISABLE, 3);
    fb_t113_cfg_gpios(GPIO_D, 12, 6, 0x2, GPIO_PULL_DISABLE, 3);
    fb_t113_cfg_gpios(GPIO_D, 18, 4, 0x2, GPIO_PULL_DISABLE, 3);

    // gpio_pull(GPIO_D,21,GPIO_PULL_UP);
    // gpio_pull(GPIO_D,20,GPIO_PULL_UP);

    // gpio_pull(GPIO_D,21,GPIO_PULL_DOWN);
    // gpio_pull(GPIO_D,20,GPIO_PULL_DOWN);

    // gpio_config(GPIO_D,19,GPIO_OUTPUT);
    // gpio_output(GPIO_D,19,0);

    // gpio_config(GPIO_D,18,GPIO_OUTPUT);
    // gpio_output(GPIO_D,18,1);

    // test gpio
    //  gpio_config(GPIO_E,3,GPIO_OUTPUT);
    //  gpio_output(GPIO_E,3,0);
  }

  // tcon init
  t113_tconlcd_disable(lcd);
  t113_tconlcd_set_timing(lcd);
  t113_tconlcd_set_dither(lcd);

  t113_tconlcd_enable(lcd);

  t113_de_set_mode(lcd);
  t113_de_enable(lcd);

  t113_de_set_address(lcd, lcd->vram[1]);

  t113_de_enable(lcd);

  u32 *buffer = vga->frambuffer;
  for (int i = 0; i < vga->framebuffer_length / 8; i++) {
    buffer[i] = 0;
  }

  log_info("t113_lcd_init end\n");

  return 0;
}
