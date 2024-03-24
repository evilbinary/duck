/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/

#include "gpio/sunxi-gpio.h"
#include "lcd.h"

#define DRVNAME "fb_ili9486"
#define WIDTH 320
#define HEIGHT 480

#define LCD_CS_CLR gpio_output(GPIO_E, 11, 0);
#define LCD_CS_SET gpio_output(GPIO_E, 11, 1);

#define LCD_SCLK_CLR gpio_output(GPIO_E, 8, 0);
#define LCD_SCLK_SET gpio_output(GPIO_E, 8, 1);

#define LCD_SDA_SET gpio_output(GPIO_E, 10, 1);
#define LCD_SDA_CLR gpio_output(GPIO_E, 10, 0);

void delay(int n) {
  for (int i = 0; i < 100 * n; i++) {
  }
}

// static inline void delay(int loops) {
//   __asm__ __volatile__(
//       "1:\n"
//       "subs %0, %1, #1\n"
//       "bne 1b"
//       : "=r"(loops)
//       : "0"(loops));
// }

void ili9488_reset() {
  ili9488_write_cmd(01);
  delay(20);
}

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

  gpio_config(GPIO_B, (7), GPIO_OUTPUT);
  // gpio_pull(GPIO_B(7), GPIO_PULL_UP);
  // gpio_drive(GPIO_B(7), 3);
  gpio_output(GPIO_B, (7), 1);

  kprintf("ili9488 3line spi init end\n");

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
  // ili9488_write_data((1 << 3) | (0 << 7) | (1 << 6) | (1 << 5));

  // ili9488_write_cmd(0x2B);  // Page Address Set (2Bh)写入SP EP
  // ili9488_write_data(0x00);
  // ili9488_write_data(0x00);
  // ili9488_write_data(0x01);  // EP 默认列的EC为479 修改为319
  // ili9488_write_data(0x3F);

  // ili9488_write_cmd(0x2A);  // Column Address Set (2Ah) 写入SC EC
  // ili9488_write_data(0x00);
  // ili9488_write_data(0x00);
  // ili9488_write_data(0x01);  // EC 默认列的EC为319(0X013F) 修改为479(0X01DF)
  // ili9488_write_data(0xdf);

  ili9488_write_cmd(0x2A);
  ili9488_write_data(0x00);
  ili9488_write_data(0x00);
  ili9488_write_data(0x01);
  ili9488_write_data(0xDF);  // 479

  ili9488_write_cmd(0x2B);
  ili9488_write_data(0x00);
  ili9488_write_data(0x00);
  ili9488_write_data(0x01);
  ili9488_write_data(0x3F);  // 319

  ili9488_write_cmd(0x3A);   // Interface Pixel Format
  ili9488_write_data(0x66);  // 18bit

  ili9488_write_cmd(0xB0);  // Interface Mode Control
  ili9488_write_data(0x00);

  kprintf("ili9488 Frame rate\n");

  ili9488_write_cmd(0xB1);   // Frame rate
  ili9488_write_data(0xA0);  // 60Hz
  // ili9488_write_data(0x0);  // 28Hz

  ili9488_write_cmd(0xB4);   // Display Inversion Control
  ili9488_write_data(0x02);  // 2-dot

  ili9488_write_cmd(0xB6);   // RGB/MCU Interface Control
  ili9488_write_data(0x32);  // MCU:02; RGB:32/22
  ili9488_write_data(0x02);  // Source,Gate scan dieection

  ili9488_write_cmd(0xE9);   // Set Image Function
  ili9488_write_data(0x00);  // disable 24 bit data input

  kprintf("ili9488 ajust control\n");

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

  // ili9488_test();

  gpio_output(GPIO_E, (11), 1);
  gpio_config(GPIO_E, 11, GPIO_DISABLE);  // cs    pe11
  gpio_config(GPIO_E, 10, GPIO_DISABLE);  // sda    pe10
  gpio_config(GPIO_E, 8, GPIO_DISABLE);   // sclk    pe8
  gpio_config(GPIO_D, 22, GPIO_DISABLE);  // blk    pd22

  kprintf("ili9488 lcd end\n");
}

void ili9488_set_cursor(u16 xpos, u16 ypos) {

  u32 w=480;
  u32 h=480;

  ili9488_write_cmd(0X2A);
  ili9488_write_data(xpos >> 8);
  ili9488_write_data(xpos & 0XFF);
  ili9488_write_data((w - 1) >> 8);
  ili9488_write_data((w - 1) & 0XFF);

  ili9488_write_cmd(0X2B);
  ili9488_write_data(ypos >> 8);
  ili9488_write_data(ypos & 0XFF);
  ili9488_write_data((h - 1) >> 8);
  ili9488_write_data((h - 1) & 0XFF);
}

void ili9488_set_pixel(u32 x, u32 y, u32 color) {
  ili9488_set_cursor(x, y);  // 设置光标位置
  ili9488_write_cmd(0X2C);   // 开始写入GRAM
  ili9488_write_data(color);
}

int ili9488_write_pixel(vga_device_t* vga, const void* buf, size_t len) {
  u16* color = buf;
  int i = 0;
  for (i = 0; i < len / 6; i += 3) {
    ili9488_set_pixel(color[i], color[i + 1], color[i + 2]);
  }
  return i;
}

void ili9488_test() {
  for (int i = 0; i < 256; i++) {
    for (int j = 0; j < 200; j++) {
      ili9488_set_pixel(i, j, 0xffff00);
    }
  }
  for (;;);
  // ili9488_fill(0, 0, 128, 128, BLUE);
  // ili9488_fill(0, 0, 128, 128, GREEN);
  // ili9488_fill(0, 0, 128, 128, RED);
  kprintf("ili9488 test lcd end\n");
  // u32* p = 0xfb000000;
  // for (int i = 0; i < 300 / 4; i++) {
  //   *p = 0xffffff;

  //   p++;
  // }
  kprintf("ili9488 test lcd end2\n");
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