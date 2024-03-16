/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/

#include "gpio/sunxi-gpio.h"
#include "lcd.h"

#define USE_BUFF 1

#define BLACK 0x0000
#define WHITE 0xFFFF

#define RED 0xf800
#define BLUE 0x001f
#define GREEN 0x07e0
#define YELLOW 0xffe0
#define MAGENTA 0xF81F
#define CYAN 0xFFE0

#define DRVNAME "fb_ili9486"
#define WIDTH 320
#define HEIGHT 480

#define LCD_CS_CLR gpio_output(GPIO_E, 11, 0);
#define LCD_SCLK_CLR gpio_output(GPIO_E, 8, 0);
#define LCD_SDA_SET gpio_output(GPIO_E, 10, 1);
#define LCD_SDA_CLR gpio_output(GPIO_E, 10, 0);
#define LCD_SCLK_SET gpio_output(GPIO_E, 8, 1);
#define LCD_CS_SET gpio_output(GPIO_E, 11, 1);

void delay(int n) {
  for (int i = 0; i < 1000 * n; i++) {
  }
}

void ili9488_reset() { delay(200); }

void ili9488_write_cmd(u8 cmd) {
  u8 i = 0;
  u16 command = 0;

  LCD_CS_CLR;
  delay(30);
  command |= cmd;

  for (i = 0; i < 9; i++) {
    LCD_SCLK_CLR;
    if (command & 0x0100) {
      LCD_SDA_SET;
    } else {
      LCD_SDA_CLR;
    }
    delay(30);
    LCD_SCLK_SET;
    delay(30);
    command <<= 1;
  }
  LCD_CS_SET;
  delay(30);
}

void ili9488_write_data(u8 data) {
  u8 i = 0;
  u16 my_data = 0x0100;

  LCD_CS_CLR;
  delay(30);
  my_data |= data;

  for (i = 0; i < 9; i++) {
    LCD_SCLK_CLR;
    if (my_data & 0x0100) {
      LCD_SDA_SET;
    } else {
      LCD_SDA_CLR;
    }
    delay(30);
    LCD_SCLK_SET;
    delay(30);
    my_data <<= 1;
  }
  LCD_CS_SET;
  delay(30);
}

void ili9488_test() {
  // ili9488_fill(0, 0, 128, 128, BLUE);
  // ili9488_fill(0, 0, 128, 128, GREEN);
  // ili9488_fill(0, 0, 128, 128, RED);
  kprintf("ili9488 test lcd end\n");
}

void ili9488_init() {
  kprintf("ili9488 init\n");

  gpio_config(GPIO_E, 11, GPIO_OUTPUT);  // cs    pe11
  gpio_config(GPIO_E, 10, GPIO_OUTPUT);  // sda    pe10
  gpio_config(GPIO_E, 8, GPIO_OUTPUT);   // sclk    pe8
  gpio_config(GPIO_D, 22, GPIO_OUTPUT);  // blk    pd22

  gpio_pull(GPIO_E, (11), GPIO_PULL_UP);
  gpio_pull(GPIO_E, (10), GPIO_PULL_UP);
  gpio_pull(GPIO_E, (8), GPIO_PULL_UP);
  gpio_pull(GPIO_D, (22), GPIO_PULL_UP);

  gpio_drive(GPIO_E, (11), 3);
  gpio_drive(GPIO_E, (10), 3);
  gpio_drive(GPIO_E, (8), 3);
  gpio_drive(GPIO_D, (22), 3);

  // gpio_config(GPIO_B, (7), GPIO_OUTPUT);
  // gpio_pull(GPIO_B(7), GPIO_PULL_UP);
  // gpio_drive(GPIO_B(7), 3);
  // gpio_config(GPIO_B, (7), 1);

  delay(20);

  // init lcd
  ili9488_reset();

  kprintf("ili9488 p gamma\n");

  ili9488_write_cmd(0xE0);  // P-Gamma
  ili9488_write_data(0x00);
  ili9488_write_data(0x10);
  ili9488_write_data(0x14);
  ili9488_write_data(0x01);
  ili9488_write_data(0x0E);
  ili9488_write_data(0x04);
  ili9488_write_data(0x33);
  ili9488_write_data(0x56);
  ili9488_write_data(0x48);
  ili9488_write_data(0x03);
  ili9488_write_data(0x0C);
  ili9488_write_data(0x0B);
  ili9488_write_data(0x2B);
  ili9488_write_data(0x34);
  ili9488_write_data(0x0F);

  kprintf("ili9488 n gamma\n");

  ili9488_write_cmd(0xE1);  // N-Gamma
  ili9488_write_data(0x00);
  ili9488_write_data(0x12);
  ili9488_write_data(0x18);
  ili9488_write_data(0x05);
  ili9488_write_data(0x12);
  ili9488_write_data(0x06);
  ili9488_write_data(0x40);
  ili9488_write_data(0x34);
  ili9488_write_data(0x57);
  ili9488_write_data(0x06);
  ili9488_write_data(0x10);
  ili9488_write_data(0x0C);
  ili9488_write_data(0x3B);
  ili9488_write_data(0x3F);
  ili9488_write_data(0x0F);

  kprintf("ili9488 power\n");

  ili9488_write_cmd(0xC0);   // Power Control 1
  ili9488_write_data(0x0F);  // Vreg1out
  ili9488_write_data(0x0C);  // Verg2out

  ili9488_write_cmd(0xC1);   // Power Control 2
  ili9488_write_data(0x41);  // VGH,VGL

  ili9488_write_cmd(0xC5);  // Power Control 3
  ili9488_write_data(0x00);
  ili9488_write_data(0x25);  // Vcom
  ili9488_write_data(0x80);

  ili9488_write_cmd(0x36);  // Memory Access
  ili9488_write_data(0x48);

  ili9488_write_cmd(0x3A);   // Interface Pixel Format
  ili9488_write_data(0x66);  // 18bit

  ili9488_write_cmd(0xB0);  // Interface Mode Control
  ili9488_write_data(0x00);

  kprintf("ili9488 Frame rate\n");

  ili9488_write_cmd(0xB1);   // Frame rate
  ili9488_write_data(0xA0);  // 60Hz

  ili9488_write_cmd(0xB4);   // Display Inversion Control
  ili9488_write_data(0x02);  // 2-dot

  ili9488_write_cmd(0xB6);   // RGB/MCU Interface Control
  ili9488_write_data(0x32);  // MCU:02; RGB:32/22
  ili9488_write_data(0x02);  // Source,Gate scan dieection

  ili9488_write_cmd(0xE9);   // Set Image Function
  ili9488_write_data(0x00);  // disable 24 bit data input

  kprintf("ili9488 Frame rate\n");

  ili9488_write_cmd(0xF7);  // A d j u s t C o n t r o l
  ili9488_write_data(0xA9);
  ili9488_write_data(0x51);
  ili9488_write_data(0x2C);
  ili9488_write_data(0x82);  // D 7 s t r e a m, l o o s e

  ili9488_write_cmd(0x21);  // Normal Black

  kprintf("ili9488 Sleep out\n");

  ili9488_write_cmd(0x11);  // Sleep out
  delay(120);
  ili9488_write_cmd(0x29);  // Display on

  // ili9488_fill(0, 0, 128, 128, BLACK);
  // ili9488_test();

  kprintf("ili9488 lcd end\n");
}

int ili9488_write_pixel(vga_device_t* vga, const void* buf, size_t len) {
  u16* color = buf;
  int i = 0;
  for (i = 0; i < len / 6; i += 3) {
    // ili9488_set_pixel(color[i], color[i + 1], color[i + 2]);
  }
  return i;
}

int lcd_init_mode(vga_device_t* vga, int mode) {
  vga->width = WIDTH;
  vga->height = HEIGHT;
  vga->bpp = 16;

  vga->mode = mode;
  vga->write = ili9488_write_pixel;
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

  ili9488_init();

  return 0;
}