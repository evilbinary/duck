#include "v3s.h"

#include "kernel/memory.h"
#include "vga/vga.h"
#include "v3s-ccu.h"
#include "v3s-de.h"
#include "v3s-tcon.h"

#ifdef SYSCONF_MODULE
#include "modules/sysconf/sysconf.h"
#endif

#define RGB24_2_RGB565(r, g, b) \
  (u16)((((r) << 8) & 0xF800) | (((g) << 3) & 0x7E0) | (((b) >> 3)))

/* 【TCON 像素时钟源】取决于 CCU_TCON_CLK(0x118) 的选源与 PLL_VIDEO 的分频。
 * 平台初始化 duck/platform/v3s/init.c 把 PLL_VIDEO 配成 396MHz（那边注释写的
 * "pll video - 396MHZ"）。若你改了 TCON 选源或 PLL_VIDEO 参数，这里要同步，
 * 否则点屏时钟会整体错掉（现象：闪 / 花 / 无图）。 */
#define V3S_TCON_SRC_HZ 396000000

#ifdef SYSCONF_MODULE
/* 本板这块 vga 设备（v3s_lcd_init 时记下）与"配置就绪"回调：
 * 驱动 init 跑在 fatfs 之前、读不到 /conf/system.conf，所以 init 里只登记回调
 * （sysconf_on_ready），由 sysconf 模块在文件系统就绪后触发它来取值+重配硬件。 */
static vga_device_t* v3s_vga = NULL;
static void v3s_lcd_apply_conf(void);
#endif

typedef struct pixel {
  u8 red;
  u8 green;
  u8 blue;
} pixel_t;

static void write_pixel(vga_device_t *vga, uint32_t x, uint32_t y,
                        const pixel_t *pix) {
  u32 color = RGB24_2_RGB565(pix->red, pix->green, pix->blue);
  vga->frambuffer[x + y * vga->width] = color;
}

static void inline v3s_de_enable(v3s_lcd_t *lcd) {
  struct de_glb_t *glb = (struct de_glb_t *)(lcd->de + V3S_DE_MUX_GLB);

  // page_map((u32)&glb->dbuff,(u32)&glb->dbuff,0);

  io_write32((u32)&glb->dbuff, 1);
}

static inline void v3s_de_set_address(v3s_lcd_t *lcd, void *vram) {
  struct de_ui_t *ui =
      (struct de_ui_t *)(lcd->de + V3S_DE_MUX_CHAN + 0x1000 * 2);
  // page_map((u32)&ui->cfg[0].top_laddr,(u32)&ui->cfg[0].top_laddr,0);

  io_write32((u32)&ui->cfg[0].top_laddr, (u32)vram);
}

static inline void v3s_de_set_mode(v3s_lcd_t *lcd) {
  struct de_clk_t *clk = (struct de_clk_t *)(lcd->de);
  struct de_glb_t *glb = (struct de_glb_t *)(lcd->de + V3S_DE_MUX_GLB);
  struct de_bld_t *bld = (struct de_bld_t *)(lcd->de + V3S_DE_MUX_BLD);
  struct de_ui_t *ui =
      (struct de_ui_t *)(lcd->de + V3S_DE_MUX_CHAN + 0x1000 * 2);
  u32 size = (((lcd->height - 1) << 16) | (lcd->width - 1));
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
    void *chan = (void *)(lcd->de + V3S_DE_MUX_CHAN + 0x1000 * i);
    kmemset(chan, 0, i == 0 ? sizeof(struct de_vi_t) : sizeof(struct de_ui_t));
  }
  kmemset(bld, 0, sizeof(struct de_bld_t));

  io_write32((u32)&bld->fcolor_ctl, 0x00000101);
  io_write32((u32)&bld->route, 2);
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

  io_write32(lcd->de + V3S_DE_MUX_VSU, 0);
  io_write32(lcd->de + V3S_DE_MUX_GSU1, 0);
  io_write32(lcd->de + V3S_DE_MUX_GSU2, 0);
  io_write32(lcd->de + V3S_DE_MUX_GSU3, 0);
  io_write32(lcd->de + V3S_DE_MUX_FCE, 0);
  io_write32(lcd->de + V3S_DE_MUX_BWS, 0);
  io_write32(lcd->de + V3S_DE_MUX_LTI, 0);
  io_write32(lcd->de + V3S_DE_MUX_PEAK, 0);
  io_write32(lcd->de + V3S_DE_MUX_ASE, 0);
  io_write32(lcd->de + V3S_DE_MUX_FCC, 0);
  io_write32(lcd->de + V3S_DE_MUX_DCSC, 0);

  io_write32((u32)&ui->cfg[0].attr,
             (1 << 0) | (4 << 8) | (1 << 1) | (0xff << 24));
  io_write32((u32)&ui->cfg[0].size, size);
  io_write32((u32)&ui->cfg[0].coord, 0);
  io_write32((u32)&ui->cfg[0].pitch, 4 * lcd->width);
  io_write32((u32)&ui->cfg[0].top_laddr, (u32)lcd->vram[lcd->index]);
  io_write32((u32)&ui->ovl_size, size);
}

static inline void v3s_tcon_enable(v3s_lcd_t *lcd) {
  struct v3s_tcon_reg_t *tcon = (struct v3s_tcon_reg_t *)lcd->tcon;
  u32 val;

  val = io_read32((u32)&tcon->ctrl);
  val |= (1 << 31);
  io_write32((u32)&tcon->ctrl, val);
}

static inline void v3s_tcon_disable(v3s_lcd_t *lcd) {
  struct v3s_tcon_reg_t *tcon = (struct v3s_tcon_reg_t *)lcd->tcon;
  u32 val;

  io_write32((u32)&tcon->ctrl, 0);
  io_write32((u32)&tcon->int0, 0);

  val = io_read32((u32)&tcon->tcon0_dclk);
  val &= ~(0xf << 28);
  io_write32((u32)&tcon->tcon0_dclk, val);

  io_write32((u32)&tcon->tcon0_io_tristate, 0xffffffff);
  io_write32((u32)&tcon->tcon1_io_tristate, 0xffffffff);
}

static inline void v3s_tcon_set_mode(v3s_lcd_t *lcd) {
  struct v3s_tcon_reg_t *tcon = (struct v3s_tcon_reg_t *)lcd->tcon;
  int bp, total;
  u32 val;

  val = io_read32((u32)&tcon->ctrl);
  val &= ~(0x1 << 0);
  io_write32((u32)&tcon->ctrl, val);

  /* 【TCON0_CTRL[8:4] = 像素时钟相位 CLK_DELAY(0..31)】
   * 老代码取 (vfp+vbp+vspw)/2（800x480 那套 = 22），这组值在实机上能出图，所以
   * 默认沿用；要微调（出现采样噪点/雪花时按 1 步进扫 0..31）就用
   * /conf/system.conf 的 [lcd] clk_delay 覆盖，不用重编内核。 */
  io_write32((u32)&tcon->tcon0_ctrl, (1 << 31) | ((lcd->clk_delay & 0x1f) << 4));

  /* 【像素时钟分频】dclk = TCON 源时钟 / (div + 1)。
   * 老代码是 `val=(vfp+vbp+vspw)/2; dclk=val/2` 的魔数：800x480 那套消隐凑出
   * 11 ⇒ 396MHz/12 = 33MHz。这里改成按面板 pclk 反推，**pclk_hz=33MHz 时算出的
   * div 与老代码逐位相同**（396/33-1 = 11），所以默认路径的寄存器值没有变化；
   * 好处是配置里换了 pclk，分频会跟着变，不用改代码。
   * 前提是 V3S_TCON_SRC_HZ 与 CCU 实际选源一致（本文件按 396MHz 估）——
   * 若不一致，只有"改用别的 pclk"那条新路会偏，"沿用 33MHz"这条路仍与老代码等价。 */
  val = V3S_TCON_SRC_HZ / lcd->timing.pixel_clock_hz;
  if (val > 0) {
    val -= 1; /* 寄存器是 div（实际分频 = div+1） */
  }
  if (val > 63) {
    val = 63;
  }
  io_write32((u32)&tcon->tcon0_dclk, (0xf << 28) | (val & 0x3f));

  io_write32((u32)&tcon->tcon0_timing_active,
             ((lcd->width - 1) << 16) | ((lcd->height - 1) << 0));

  bp = lcd->timing.h_sync_len + lcd->timing.h_back_porch;
  total = lcd->width + lcd->timing.h_front_porch + bp;
  io_write32((u32)&tcon->tcon0_timing_h, ((total - 1) << 16) | ((bp - 1) << 0));
  bp = lcd->timing.v_sync_len + lcd->timing.v_back_porch;
  total = lcd->height + lcd->timing.v_front_porch + bp;
  io_write32((u32)&tcon->tcon0_timing_v, ((total * 2) << 16) | ((bp - 1) << 0));
  io_write32((u32)&tcon->tcon0_timing_sync,
             ((lcd->timing.h_sync_len - 1) << 16) |
                 ((lcd->timing.v_sync_len - 1) << 0));

  io_write32((u32)&tcon->tcon0_hv_intf, 0);
  io_write32((u32)&tcon->tcon0_cpu_intf, 0);

  if (lcd->bits_per_pixel == 18 || lcd->bits_per_pixel == 16) {
    io_write32((u32)&tcon->tcon0_frm_seed[0], 0x11111111);
    io_write32((u32)&tcon->tcon0_frm_seed[1], 0x11111111);
    io_write32((u32)&tcon->tcon0_frm_seed[2], 0x11111111);
    io_write32((u32)&tcon->tcon0_frm_seed[3], 0x11111111);
    io_write32((u32)&tcon->tcon0_frm_seed[4], 0x11111111);
    io_write32((u32)&tcon->tcon0_frm_seed[5], 0x11111111);
    io_write32((u32)&tcon->tcon0_frm_table[0], 0x01010000);
    io_write32((u32)&tcon->tcon0_frm_table[1], 0x15151111);
    io_write32((u32)&tcon->tcon0_frm_table[2], 0x57575555);
    io_write32((u32)&tcon->tcon0_frm_table[3], 0x7f7f7777);
    io_write32((u32)&tcon->tcon0_frm_ctrl, (lcd->bits_per_pixel == 18)
                                               ? ((1 << 31) | (0 << 4))
                                               : ((1 << 31) | (5 << 4)));
  }

  val = (1 << 28);
  if (!lcd->timing.h_sync_active) val |= (1 << 25);
  if (!lcd->timing.v_sync_active) val |= (1 << 24);
  if (!lcd->timing.den_active) val |= (1 << 27);
  if (!lcd->timing.clk_active) val |= (1 << 26);
  io_write32((u32)&tcon->tcon0_io_polarity, val);
  io_write32((u32)&tcon->tcon0_io_tristate, 0);
}

/* 【把帧缓冲 VA→PA 映射铺一遍】长度取 vga->framebuffer_length。
 * v3s_lcd_init() 与配置应用（v3s_lcd_apply_conf）共用：分辨率变大时要补映射。 */
static void v3s_lcd_map_fb(vga_device_t *vga) {
  /* 内核页表映射 + 登记 fb VMA；分辨率变化时重复调用只更新几何，不重复注入。 */
  vmemory_map_phys((u32)(uintptr_t)vga->frambuffer,
                   (u32)(uintptr_t)vga->pframbuffer, vga->framebuffer_length,
                   MEMORY_FB, PAGE_FB);
}

/* 【DE/TCON 起振序列】面板参数一变就得按这个顺序重来一遍：
 * 关 TCON → 配 DE(模式+使能) → 配 TCON(时序+分频) → 开 TCON → 设扫描地址。
 * 只在"还没开始画"的时候调用（屏幕会黑一下再恢复，DE 重新同步）。 */
static void v3s_lcd_bringup(v3s_lcd_t *lcd) {
  /* todo clk video enable / todo gpio set —— 见 v3s_lcd_init 里的说明 */
  v3s_tcon_disable(lcd);
  v3s_de_set_mode(lcd);
  v3s_de_enable(lcd);
  v3s_tcon_set_mode(lcd);
  v3s_tcon_enable(lcd);

  v3s_de_set_address(lcd, lcd->vram[lcd->index]);
  v3s_de_enable(lcd);
}

int v3s_lcd_init(vga_device_t *vga) {
  log_info("v3s_lcd_init\n");
  v3s_lcd_t *lcd = kmalloc(sizeof(v3s_lcd_t),DEFAULT_TYPE);
  vga->priv = lcd;
#ifdef SYSCONF_MODULE
  v3s_vga = vga;
#endif

  lcd->de = V3S_DE_BASE;
  lcd->tcon = V3S_TCON_BASE;

  lcd->rstde = 44;
  lcd->rsttcon = 36;
  lcd->width = vga->width;
  lcd->height = vga->height;
  /* 【面板参数：下面这一组是实测能用的老参数（800x480 那套消隐 + 33MHz）】
   * 字段映射：le→h_back_porch  ri→h_front_porch  up→v_back_porch
   *           lo→v_front_porch  hs→h_sync_len     vs→v_sync_len
   *   "x:800,y:480,depth:18,pclk_khz:33000,le:87,ri:40,up:31,lo:13,hs:1,vs:1"
   * 【换屏/换分辨率不要改这里】用 /conf/system.conf 的 [lcd] 段覆盖（sysconf 模块
   * 在 fatfs 之后下发），改文件重启即可，不用重编内核。
   * 【历史】这里一度被改成 480x272 那套（42/8/1、11/4/1、10MHz、bpp18），v3s 上
   * 结果黑屏 —— 见下面 tcon0_dclk 处关于分频与 396MHz 前提的说明。 */
  lcd->bits_per_pixel = 16;  /* 老参数：走 16bpp 那条 FRM/dither 路（18 是另一条） */
  lcd->bytes_per_pixel = 4;  /* DE UI 侧是 32bpp（pitch = 4*width） */
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

  /* CLK_DELAY 默认沿用老代码的凑法：(vfp+vbp+vspw)/2（这组 = 22）。
   * 配置里给了 clk_delay 就在 v3s_lcd_apply_conf() 里覆盖它。 */
  lcd->clk_delay = (lcd->timing.v_front_porch + lcd->timing.v_back_porch +
                    lcd->timing.v_sync_len) /
                   2;

  // map tcon 4k
  page_map(V3S_TCON_BASE, V3S_TCON_BASE, PAGE_DEV);
  // map ccu 1k
  page_map(V3S_CCU_BASE, V3S_CCU_BASE, PAGE_DEV);

  // map de 2m
  u32 addr = V3S_DE_BASE;
  for (int i = 0; i < 1024 * 1024 * 2 / PAGE_SIZE; i++) {
    page_map(addr, addr, PAGE_DEV);
    addr += 0x1000;
  }

  // vga->pframbuffer=kmalloc(vga->framebuffer_length*2,DEFAULT_TYPE);
  // map fb
  v3s_lcd_map_fb(vga);

  // init
  // todo clk video enable

  // todo gpio set
  // V3S_GPIOE0 set 3
  // V3S_GPIOE23 set 3

  // tcon init
  v3s_lcd_bringup(lcd);
  log_info("v3s_lcd_init end\n");

#ifdef SYSCONF_MODULE
  /* 登记"配置就绪"回调：sysconf 模块（注册在 fatfs 之后）跑到时会调它，
   * 由本驱动自己去 /conf/system.conf 的 [lcd] 段取值并重配 DE/TCON。 */
  sysconf_on_ready(v3s_lcd_apply_conf);
#endif
}

#ifdef SYSCONF_MODULE
/* 【应用 /conf/system.conf 的 [lcd] 段】—— 由 sysconf 模块在文件系统就绪后回调
 * （登记见 v3s_lcd_init 里的 sysconf_on_ready）。分层上"参数怎么落到硬件"完全在
 * 本驱动内：直接 sysconf_get_int() 取值、直接改自己的 v3s_lcd_t/vga_device_t，
 * 不再经过 ioctl、也不需要 sysconf 去找设备（它现在不认识任何显示设备）。
 *
 * 【没配就不动】每个键的 def 传"当前值"，缺键 ⇒ 保持驱动内置默认；一个键都没配
 * 时连 bringup 都不跑（重跑会重新起振 DE/TCON，没必要就别动）。
 * clk_delay 例外：0 是合法相位，所以用 -1 当"没配"的哨兵。 */
static void v3s_lcd_apply_conf(void) {
  vga_device_t *vga = v3s_vga;
  v3s_lcd_t *lcd;
  int v;
  int configured = 0;
  u32 test_color;
  u32* fb;
  u32 i;
  u32 n;

  if (vga == NULL || vga->priv == NULL) {
    return;
  }
  lcd = (v3s_lcd_t *)vga->priv;

  v = sysconf_get_int("lcd", "width", -1);
  if (v > 0) {
    vga->width = (u32)v;
    configured = 1;
  }
  v = sysconf_get_int("lcd", "height", -1);
  if (v > 0) {
    vga->height = (u32)v;
    configured = 1;
  }
  v = sysconf_get_int("lcd", "bpp", -1);
  if (v > 0) {
    lcd->bits_per_pixel = v;
    configured = 1;
  }
  v = sysconf_get_int("lcd", "pclk_hz", -1);
  if (v > 0) {
    lcd->timing.pixel_clock_hz = v;
    configured = 1;
  }
  v = sysconf_get_int("lcd", "hbp", -1);
  if (v > 0) {
    lcd->timing.h_back_porch = v;
    configured = 1;
  }
  v = sysconf_get_int("lcd", "hfp", -1);
  if (v > 0) {
    lcd->timing.h_front_porch = v;
    configured = 1;
  }
  v = sysconf_get_int("lcd", "hspw", -1);
  if (v > 0) {
    lcd->timing.h_sync_len = v;
    configured = 1;
  }
  v = sysconf_get_int("lcd", "vbp", -1);
  if (v > 0) {
    lcd->timing.v_back_porch = v;
    configured = 1;
  }
  v = sysconf_get_int("lcd", "vfp", -1);
  if (v > 0) {
    lcd->timing.v_front_porch = v;
    configured = 1;
  }
  v = sysconf_get_int("lcd", "vspw", -1);
  if (v > 0) {
    lcd->timing.v_sync_len = v;
    configured = 1;
  }
  v = sysconf_get_int("lcd", "clk_delay", -1);
  if (v >= 0) {
    lcd->clk_delay = v & 0x1f;
    configured = 1;
  }

  if (configured) {
    lcd->width = vga->width;
    lcd->height = vga->height;
    /* 长度跟着分辨率重算（*8 是历史映射余量，与其它平台保持一致） */
    vga->framebuffer_length =
        lcd->width * lcd->height * lcd->bytes_per_pixel * 8;

    v3s_lcd_map_fb(vga);
    v3s_lcd_bringup(lcd);

    log_info(
        "v3s lcd conf: %dx%d bpp=%d pclk=%d hbp=%d hfp=%d hspw=%d vbp=%d vfp=%d "
        "vspw=%d clk_delay=%d\n",
        lcd->width, lcd->height, lcd->bits_per_pixel,
        lcd->timing.pixel_clock_hz, lcd->timing.h_back_porch,
        lcd->timing.h_front_porch, lcd->timing.h_sync_len,
        lcd->timing.v_back_porch, lcd->timing.v_front_porch,
        lcd->timing.v_sync_len, lcd->clk_delay);
  } else {
    log_debug("v3s: no [lcd] config, keep built-in panel params\n");
  }

  /* 【点屏自检】[lcd] test_color = 0xFFFFFF：把整屏填成纯色。
   * 驱动手里就有帧缓冲指针（vga->frambuffer），直接写，不需要任何 ioctl：
   *   屏上出现纯色 ⇒ DE/TCON/面板/背光这条链全通，黑屏只是没有应用往上画；
   *   仍然黑 ⇒ 时序/时钟/背光问题。0xFFFFFF / 0xFFFFFFFF 在任何格式下都是白。 */
  test_color = (u32)sysconf_get_int("lcd", "test_color", 0);
  fb = vga->frambuffer;
  if (test_color != 0 && fb != NULL && lcd->width > 0 && lcd->height > 0) {
    n = (u32)lcd->width * (u32)lcd->height;
    for (i = 0; i < n; i++) {
      fb[i] = test_color; /* DE 的 UI 通道是 32bpp（pitch = 4*width） */
    }
    log_info("v3s: test_color %x -> %dx%d @%x\n", test_color, lcd->width,
             lcd->height, (u32)(uintptr_t)fb);
  }
}
#endif /* SYSCONF_MODULE */

void gpu_flush(vga_device_t *vga, u32 index) {
  vga->framebuffer_index = index;
  v3s_lcd_t *lcd = vga->priv;
  v3s_de_enable(lcd);
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

  /* 【不要在这里强制面板尺寸】这里一度被改成 vga->width/height = 480x272
   * （去配合那套 480x272 时序），v3s 上结果黑屏，已回退成沿用 mode 给的尺寸。
   * 要换尺寸同样走 /conf/system.conf 的 [lcd] width/height（v3s_lcd_apply_conf 会
   * 同步重算 framebuffer_length 并补页映射），不要改这里。 */

  vga->framebuffer_index = 0;
  vga->framebuffer_count = 1;
  vga->pframbuffer = 0x73e00000;
  vga->frambuffer = 0xfb000000;

  v3s_lcd_init(vga);

  log_info("fb addr:%x end:%x len:%x\n", vga->frambuffer,vga->frambuffer+vga->framebuffer_length, vga->framebuffer_length);


  // u8 *buffer = vga->frambuffer;
  // for (int i = 0; i < vga->framebuffer_length / 8; i++) {
  //   buffer[i] = 0x00;
  // }

  return 0;
}

// static void test_pixel(vga_device_t *vga) {
//   static const pixel_t BLACK = {0xff, 0x00, 0x00};
//   static const pixel_t RED = {0x00, 0x00, 0xff};

//   for (int i = 0; i < 300; i++) {
//     write_pixel(vga, i, 20, &BLACK);
//     write_pixel(vga, i, 40, &RED);
//     write_pixel(vga, i, 180, &BLACK);
//     write_pixel(vga, i, 190, &RED);
//   }
//   for (uint32_t j = 0; j < vga->height; j++) {
//     for (uint32_t i = 0; i < vga->width; i++) {
//       write_pixel(vga, i, j, &BLACK);
//     }
//   }
// }