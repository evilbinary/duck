/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/

#include "gpio/sunxi-gpio.h"
#include "lcd.h"
#include "spi/spi.h"
#include "spi/sunxi-spi.h"

#define WHITE 0xFFFF
#define BLACK 0x0000
#define BLUE 0x001F
#define BRED 0XF81F
#define GRED 0XFFE0
#define GBLUE 0X07FF
#define RED 0xF800
#define MAGENTA 0xF81F
#define GREEN 0x07E0
#define CYAN 0x7FFF
#define YELLOW 0xFFE0
#define BROWN 0XBC40  // 棕色
#define BRRED 0XFC07  // 棕红色
#define GRAY 0X8430   // 灰色

#define WIDTH 240
#define HEIGHT 320

#define SPI0 0
#define SPI0_BASE 0x01C68000
#define SPI1_BASE 0x01C68000

static sunxi_spi_t* spio_base[] = {
    (sunxi_spi_t*)SPI0_BASE,  // spi 0
    (sunxi_spi_t*)SPI1_BASE,  // spi 1
};

#define LCD_CS_SET gpio_output(GPIO_C, 2, 1);
#define LCD_CS_CLR gpio_output(GPIO_C, 2, 0);

#define LCD_RS_SET gpio_output(GPIO_C, 0, 1);
#define LCD_RS_CLR gpio_output(GPIO_C, 0, 0);

#define LCD_RSET_SET gpio_output(GPIO_B, 2, 1);
#define LCD_RSET_CLR gpio_output(GPIO_B, 2, 0);

static void delay(int n) {
  for (int i = 0; i < 100 * n; i++) {
  }
}

void st7789_reset() {
  LCD_RSET_CLR;
  delay(100);
  LCD_RSET_SET;
  delay(50);
}

void st7789_write_cmd(u8 cmd) {
  LCD_RS_CLR;
  sunxi_spi_write(SPI0, &cmd, 1);
}

void st7789_write_data(u8 data) {
  LCD_RS_SET;
  sunxi_spi_write(SPI0, &data, 1);
}

void st7789_write_data_word(u16 data) {
  LCD_RS_SET;
  sunxi_spi_write(SPI0, &data, 2);
}

void st7789_address_set(u16 x1, u16 y1, u16 x2, u16 y2) {
  st7789_write_cmd(0x2a);
  st7789_write_data_word(x1);
  st7789_write_data_word(x2);

  st7789_write_cmd(0x2b);
  st7789_write_data_word(y1);
  st7789_write_data_word(y2);
  st7789_write_cmd(0x2C);
}

void st7789_fill(u16 xsta, u16 ysta, u16 xend, u16 yend, u16 color) {
  u16 i, j;
  st7789_address_set(xsta, ysta, xend - 1, yend - 1);  // 设置显示范围
  // LCD_RS_SET;
  for (i = ysta; i < yend; i++) {
    for (j = xsta; j < xend; j++) {
      st7789_write_data(color);
    }
  }
}

void st7789_init() {
  kprintf("st7789 init\n");

  gpio_config(GPIO_C, 0, GPIO_OUTPUT);  // RS      PC0
  gpio_config(GPIO_C, 1, 3);            // SPI_CLK PC1
  gpio_config(GPIO_C, 2, 3);            // SPI_CS  PC2
  gpio_config(GPIO_C, 3, 3);            // SPI_MOSI PC3
  gpio_config(GPIO_B, 2, GPIO_OUTPUT);  // RST      PB2

  kprintf("st7789 spi gipio end\n");

  delay(20);

  // init lcd
  st7789_reset();

  // use SPI0_BASE
  sunxi_spi_set_base(spio_base);
  sunxi_spi_init(SPI0);
  sunxi_spi_cs(SPI0, 1);

  int rate = 240 * 320 * 60;
  rate = 240 * 240 * 60;

  // rate = 24000000 4608000;
  kprintf("st7789 spi rate %d\n", rate);

  sunxi_spi_rate(SPI0, rate);  // 240x320x60

  kprintf("st7789 spi init end\n");

  kprintf("st7789 lcd reset\n");

  // delay(120);

  // st7789_write_cmd(0x11);  // sleep out

  // delay(120);

  // st7789_write_cmd(0xB2);  // 帧率
  // st7789_write_data(0x0C);
  // st7789_write_data(0x0C);
  // st7789_write_data(0x00);
  // st7789_write_data(0x33);
  // st7789_write_data(0x33);

  // st7789_write_cmd(0x35);
  // st7789_write_data(0x00);

  // st7789_write_cmd(0x36);
  // st7789_write_data(0x00);

  // st7789_write_cmd(0x3A);   // 65k mode
  // st7789_write_data(0x05);  // RGB 5-6-5-bit Input

  // st7789_write_cmd(0xB7);
  // st7789_write_data(0x35);

  // st7789_write_cmd(0xBB);
  // st7789_write_data(0x34);

  // st7789_write_cmd(0xC0);
  // st7789_write_data(0x2C);

  // st7789_write_cmd(0xC2);
  // st7789_write_data(0x01);

  // st7789_write_cmd(0xC3);
  // st7789_write_data(0x13);  // 4.5V

  // st7789_write_cmd(0xC4);
  // st7789_write_data(0x20);

  // st7789_write_cmd(0xC6);
  // st7789_write_data(0x0F);

  // st7789_write_cmd(0xD0);
  // st7789_write_data(0xA4);
  // st7789_write_data(0xA1);

  // st7789_write_cmd(0xD6);
  // st7789_write_data(0xA1);

  // st7789_write_cmd(0xE0);
  // st7789_write_data(0xD0);
  // st7789_write_data(0x0A);
  // st7789_write_data(0x10);
  // st7789_write_data(0x0C);
  // st7789_write_data(0x0C);
  // st7789_write_data(0x18);
  // st7789_write_data(0x35);
  // st7789_write_data(0x43);
  // st7789_write_data(0x4D);
  // st7789_write_data(0x39);
  // st7789_write_data(0x13);
  // st7789_write_data(0x13);
  // st7789_write_data(0x2D);
  // st7789_write_data(0x34);

  // st7789_write_cmd(0xE1);
  // st7789_write_data(0xD0);
  // st7789_write_data(0x05);
  // st7789_write_data(0x0B);
  // st7789_write_data(0x06);
  // st7789_write_data(0x05);
  // st7789_write_data(0x02);
  // st7789_write_data(0x35);
  // st7789_write_data(0x43);
  // st7789_write_data(0x4D);
  // st7789_write_data(0x16);
  // st7789_write_data(0x15);
  // st7789_write_data(0x15);
  // st7789_write_data(0x2E);
  // st7789_write_data(0x32);

  // st7789_write_cmd(0x21);

  // st7789_write_cmd(0x29);  // turn display on

  // st7789_write_cmd(0x2C);




  //************* Start Initial Sequence **********//
  st7789_write_cmd(0x11);  // Sleep out
  delay(120);              // Delay 120ms
  //************* Start Initial Sequence **********//
  st7789_write_cmd(0x36);


  kprintf("st7789 lcd 1\n");

  // if (USE_HORIZONTAL == 0)
  //   st7789_write_data(0x00);
  // else if (USE_HORIZONTAL == 1)
  //   st7789_write_data(0xC0);
  // else if (USE_HORIZONTAL == 2)
  //   st7789_write_data(0x70);
  // else
  st7789_write_data(0xA0);

  st7789_write_cmd(0x3A);
  st7789_write_data(0x05);

  kprintf("st7789 lcd 2\n");

  st7789_write_cmd(0xB2);
  st7789_write_data(0x0C);
  st7789_write_data(0x0C);
  st7789_write_data(0x00);
  st7789_write_data(0x33);
  st7789_write_data(0x33);

  kprintf("st7789 lcd 3\n");

  st7789_write_cmd(0xB7);
  st7789_write_data(0x35);

  st7789_write_cmd(0xBB);
  st7789_write_data(0x32);  // Vcom=1.35V

  st7789_write_cmd(0xC2);
  st7789_write_data(0x01);

  st7789_write_cmd(0xC3);
  st7789_write_data(0x15);  // GVDD=4.8V  颜色深度

  st7789_write_cmd(0xC4);
  st7789_write_data(0x20);  // VDV, 0x20:0v

  st7789_write_cmd(0xC6);
  st7789_write_data(0x0F);  // 0x0F:60Hz

  st7789_write_cmd(0xD0);
  st7789_write_data(0xA4);
  st7789_write_data(0xA1);

  st7789_write_cmd(0xE0);
  st7789_write_data(0xD0);
  st7789_write_data(0x08);
  st7789_write_data(0x0E);
  st7789_write_data(0x09);
  st7789_write_data(0x09);
  st7789_write_data(0x05);
  st7789_write_data(0x31);
  st7789_write_data(0x33);
  st7789_write_data(0x48);
  st7789_write_data(0x17);
  st7789_write_data(0x14);
  st7789_write_data(0x15);
  st7789_write_data(0x31);
  st7789_write_data(0x34);

  st7789_write_cmd(0xE1);
  st7789_write_data(0xD0);
  st7789_write_data(0x08);
  st7789_write_data(0x0E);
  st7789_write_data(0x09);
  st7789_write_data(0x09);
  st7789_write_data(0x15);
  st7789_write_data(0x31);
  st7789_write_data(0x33);
  st7789_write_data(0x48);
  st7789_write_data(0x17);
  st7789_write_data(0x14);
  st7789_write_data(0x15);
  st7789_write_data(0x31);
  st7789_write_data(0x34);
  st7789_write_cmd(0x21);

  st7789_write_cmd(0x29);

  kprintf("st7789 lcd args end\n");

  st7789_test();

  kprintf("st7789 lcd end\n");
}

void st7789_set_pixel(u32 x, u32 y, u32 color) {
  st7789_address_set(x, y, 1 + x, y + 1);  // 设置光标位置
  st7789_write_data(color);
}

int st7789_write_pixel(vga_device_t* vga, const void* buf, size_t len) {
  u16* color = buf;
  int i = 0;
  for (i = 0; i < len / 6; i += 3) {
    st7789_set_pixel(color[i], color[i + 1], color[i + 2]);
  }
  return i;
}

void st7789_test() {
  kprintf("st7789 test start\n");

  for (int i = 0; i < 200; i++) {
    for (int j = 0; j < 200; j++) {
      st7789_set_pixel(i, j, 0x00ffff);
    }
  }

  kprintf("st7789 test 1\n");

  st7789_fill(0, 0, 240, 240, 0);
  kprintf("st7789 test 2\n");

  st7789_fill(0, 0, 128, 128, BLUE);
  st7789_fill(0, 0, 128, 128, GREEN);
  st7789_fill(0, 0, 128, 128, RED);

  kprintf("st7789 test lcd end\n");
  // u32* p = 0xfb000000;
  // for (int i = 0; i < 300 / 4; i++) {
  //   *p = 0xffffff;

  //   p++;
  // }
  kprintf("st7789 test lcd end2\n");
}

int lcd_init_mode(vga_device_t* vga, int mode) {
  vga->width = WIDTH;
  vga->height = HEIGHT;
  vga->bpp = 16;

  vga->mode = mode;
  vga->write = st7789_write_pixel;
  // vga->flip_buffer=gpu_flush;

  vga->framebuffer_index = 0;
  vga->framebuffer_count = 1;
  vga->frambuffer = NULL;
  vga->pframbuffer = vga->frambuffer;

  // frambuffer
  // device_t* fb_dev = device_find(DEVICE_LCD);

  // if (fb_dev != NULL) {
  //   vnode_t* frambuffer = vfs_create_node("fb", V_FILE);
  //   vfs_mount(NULL, "/dev", frambuffer);
  //   frambuffer->device = fb_dev;
  //   frambuffer->op = &device_operator;
  // } else {
  //   log_error("dev fb not found\n");
  // }

  st7789_init();

  return 0;
}