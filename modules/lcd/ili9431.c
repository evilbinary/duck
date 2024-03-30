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

void ili9431_reset() {
  LCD_RSET_CLR;
  delay(100);
  LCD_RSET_SET;
  delay(50);
}

void ili9431_write_cmd(u8 cmd) {
  LCD_RS_CLR;
  spi_msg_t msg;
  msg.tx_buf = &cmd;
  msg.tx_len = 1;
  sunxi_spi_write(SPI0, &msg, 1);
}

void ili9431_write_data(u8 data) {
  LCD_RS_SET;
  spi_msg_t msg;
  msg.tx_buf = &data;
  msg.tx_len = 1;
  sunxi_spi_write(SPI0, &msg, 1);
}

void ili9431_write_data_word(u16 data) {
  LCD_RS_SET;
  spi_msg_t msg = {0};
  msg.tx_buf = &data;
  msg.tx_len = 2;
  sunxi_spi_write(SPI0, &msg, 1);
}

void ili9431_address_set(u16 x1, u16 y1, u16 x2, u16 y2) {
  ili9431_write_cmd(0x2a);
  ili9431_write_data_word(x1);
  ili9431_write_data_word(x2);

  ili9431_write_cmd(0x2b);
  ili9431_write_data_word(y1);
  ili9431_write_data_word(y2);
  ili9431_write_cmd(0x2C);
}

void ili9431_fill(u16 xsta, u16 ysta, u16 xend, u16 yend, u16 color) {
  u16 i, j;
  ili9431_address_set(xsta, ysta, xend - 1, yend - 1);  // 设置显示范围
  for (i = ysta; i < yend; i++) {
    for (j = xsta; j < xend; j++) {
      ili9431_write_data(color);
    }
  }
}

void ili9431_init() {
  kprintf("ili9431 init\n");

  gpio_config(GPIO_C, 0, 3);            // RS      PC0
  gpio_config(GPIO_C, 1, 3);            // SPI_CLK PC1
  gpio_config(GPIO_C, 2, 3);            // SPI_CS  PC2
  gpio_config(GPIO_C, 3, 3);            // SPI_MOSI PC3
  gpio_config(GPIO_B, 2, GPIO_OUTPUT);  // RST      PB2

  kprintf("ili9431 spi gipio end\n");

  delay(20);

  // use SPI0_BASE
  sunxi_spi_set_base(spio_base);
  sunxi_spi_init(SPI0);
  sunxi_spi_cs(SPI0, 1);

  kprintf("ili9431 spi init end\n");

  // init lcd
  ili9431_reset();

  kprintf("ili9431 lcd reset\n");

  ili9431_write_cmd(0xCF);
  ili9431_write_data(0x00);
  ili9431_write_data(0xC1);
  ili9431_write_data(0X30);
  ili9431_write_cmd(0xED);
  ili9431_write_data(0x64);
  ili9431_write_data(0x03);
  ili9431_write_data(0X12);
  ili9431_write_data(0X81);
  ili9431_write_cmd(0xE8);
  ili9431_write_data(0x85);
  ili9431_write_data(0x10);
  ili9431_write_data(0x7A);
  ili9431_write_cmd(0xCB);
  ili9431_write_data(0x39);
  ili9431_write_data(0x2C);
  ili9431_write_data(0x00);
  ili9431_write_data(0x34);
  ili9431_write_data(0x02);
  ili9431_write_cmd(0xF7);
  ili9431_write_data(0x20);
  ili9431_write_cmd(0xEA);
  ili9431_write_data(0x00);
  ili9431_write_data(0x00);
  ili9431_write_cmd(0xC0);   // Power control
  ili9431_write_data(0x1B);  // VRH[5:0]
  ili9431_write_cmd(0xC1);   // Power control
  ili9431_write_data(0x01);  // SAP[2:0];BT[3:0]
  ili9431_write_cmd(0xC5);   // VCM control
  ili9431_write_data(0x30);  // 3F
  ili9431_write_data(0x30);  // 3C
  ili9431_write_cmd(0xC7);   // VCM control2
  ili9431_write_data(0XB7);
  ili9431_write_cmd(0x36);  // Memory Access Control
  ili9431_write_data(0x48);
  ili9431_write_cmd(0x3A);
  ili9431_write_data(0x55);
  ili9431_write_cmd(0xB1);
  ili9431_write_data(0x00);
  ili9431_write_data(0x1A);
  ili9431_write_cmd(0xB6);  // Display Function Control
  ili9431_write_data(0x0A);
  ili9431_write_data(0xA2);
  ili9431_write_cmd(0xF2);  // 3Gamma Function Disable
  ili9431_write_data(0x00);
  ili9431_write_cmd(0x26);  // Gamma curve selected
  ili9431_write_data(0x01);
  ili9431_write_cmd(0xE0);  // Set Gamma
  ili9431_write_data(0x0F);
  ili9431_write_data(0x2A);
  ili9431_write_data(0x28);
  ili9431_write_data(0x08);
  ili9431_write_data(0x0E);
  ili9431_write_data(0x08);
  ili9431_write_data(0x54);
  ili9431_write_data(0XA9);
  ili9431_write_data(0x43);
  ili9431_write_data(0x0A);
  ili9431_write_data(0x0F);
  ili9431_write_data(0x00);
  ili9431_write_data(0x00);
  ili9431_write_data(0x00);
  ili9431_write_data(0x00);
  ili9431_write_cmd(0XE1);  // Set Gamma
  ili9431_write_data(0x00);
  ili9431_write_data(0x15);
  ili9431_write_data(0x17);
  ili9431_write_data(0x07);
  ili9431_write_data(0x11);
  ili9431_write_data(0x06);
  ili9431_write_data(0x2B);
  ili9431_write_data(0x56);
  ili9431_write_data(0x3C);
  ili9431_write_data(0x05);
  ili9431_write_data(0x10);
  ili9431_write_data(0x0F);
  ili9431_write_data(0x3F);
  ili9431_write_data(0x3F);
  ili9431_write_data(0x0F);
  ili9431_write_cmd(0x2B);
  ili9431_write_data(0x00);
  ili9431_write_data(0x00);
  ili9431_write_data(0x01);
  ili9431_write_data(0x3f);
  ili9431_write_cmd(0x2A);
  ili9431_write_data(0x00);
  ili9431_write_data(0x00);
  ili9431_write_data(0x00);
  ili9431_write_data(0xef);
  ili9431_write_cmd(0x11);  // Exit Sleep
  delay(120);
  ili9431_write_cmd(0x29);  // display on

  kprintf("ili9431 lcd args end\n");

  ili9431_test();

  // ili9431_fill(0, 0, 240, 240, 0);

  kprintf("ili9431 lcd end\n");
}

void ili9431_set_pixel(u32 x, u32 y, u32 color) {
  ili9431_address_set(x, y, 1 + x, y + 1);  // 设置光标位置
  ili9431_write_data(color);
}

int ili9431_write_pixel(vga_device_t* vga, const void* buf, size_t len) {
  u16* color = buf;
  int i = 0;
  for (i = 0; i < len / 6; i += 3) {
    ili9431_set_pixel(color[i], color[i + 1], color[i + 2]);
  }
  return i;
}

void ili9431_test() {
  kprintf("ili9431 test start\n");

  // for (int i = 0; i < 256; i++) {
  //   for (int j = 0; j < 200; j++) {
  //     ili9431_set_pixel(i, j, 0xffff00);
  //   }
  // }
  kprintf("ili9431 test 1\n");

  ili9431_fill(0, 0, 128, 128, BLUE);
  ili9431_fill(0, 0, 128, 128, GREEN);
  ili9431_fill(0, 0, 128, 128, RED);

  kprintf("ili9431 test lcd end\n");
  // u32* p = 0xfb000000;
  // for (int i = 0; i < 300 / 4; i++) {
  //   *p = 0xffffff;

  //   p++;
  // }
  kprintf("ili9431 test lcd end2\n");
}

int lcd_init_mode(vga_device_t* vga, int mode) {
  vga->width = WIDTH;
  vga->height = HEIGHT;
  vga->bpp = 16;

  vga->mode = mode;
  vga->write = ili9431_write_pixel;
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

  ili9431_init();

  return 0;
}