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

#define CPU_SPI 1

int horizontal_mode = 0;

static sunxi_spi_t* spio_base[] = {
    (sunxi_spi_t*)SPI0_BASE,  // spi 0
    (sunxi_spi_t*)SPI1_BASE,  // spi 1
};

#define LCD_CS_SET gpio_output(GPIO_C, 2, 1);
#define LCD_CS_CLR gpio_output(GPIO_C, 2, 0);

#define LCD_DC_SET gpio_output(GPIO_C, 0, 1);
#define LCD_DC_CLR gpio_output(GPIO_C, 0, 0);

#define LCD_RSET_SET gpio_output(GPIO_B, 2, 1);
#define LCD_RSET_CLR gpio_output(GPIO_B, 2, 0);

#define SPI_CS_0 gpio_output(GPIO_C, 2, 0);
#define SPI_CS_1 gpio_output(GPIO_C, 2, 1);

#define SPI_SCK_0 gpio_output(GPIO_C, 1, 0);
#define SPI_SCK_1 gpio_output(GPIO_C, 1, 1);

#define SPI_DC_0 gpio_output(GPIO_C, 0, 0);
#define SPI_DC_1 gpio_output(GPIO_C, 0, 1);

#define SPI_SDA_0 gpio_output(GPIO_C, 3, 0);
#define SPI_SDA_1 gpio_output(GPIO_C, 3, 1);

#define SPI_RST_0 gpio_output(GPIO_B, 2, 0);
#define SPI_RST_1 gpio_output(GPIO_B, 2, 1);

static void delay(int n) {
  for (int i = 0; i < 100 * n; i++) {
  }
}

void st7789_reset() {
#ifdef CPU_SPI
  SPI_RST_0;
  delay(10);
  SPI_RST_1;
  delay(120);
#else
  LCD_RSET_CLR;
  delay(100);
  LCD_RSET_SET;
  delay(50);
#endif
}

// SCL空闲时低电平，第一个上升沿采样
void st7789_cpu_send_byte(u8 byte) {
  int i = 0;
  SPI_CS_0;
  // delay(3);
  for (i = 0; i < 8; i++) {
    SPI_SCK_0;
    if ((byte & 0x80) == 0) {
      SPI_SDA_0;
    } else {
      SPI_SDA_1;
    }
    // delay(3);
    SPI_SCK_1;
    // delay(3);
    byte <<= 1;
  }
  SPI_SCK_0;
  SPI_CS_1;
  // SPI_SDA_1
  // SPI_SCK_1;
  // SPI_SCK_0;
  // delay(3);
}

void st7789_write_cmd(u8 cmd) {
#ifdef CPU_SPI
  SPI_DC_0;
  st7789_cpu_send_byte(cmd);
#else
  LCD_CS_CLR;
  LCD_DC_CLR;
  sunxi_spi_write(SPI0, &cmd, 1);
#endif
}

void st7789_write_data(u8 data) {
#ifdef CPU_SPI
  SPI_DC_1;
  st7789_cpu_send_byte(data);
#else
  LCD_CS_CLR;
  LCD_DC_SET;
  sunxi_spi_write(SPI0, &data, 1);
#endif
}

void st7789_write_data16(u16 data) {
  st7789_write_data(data >> 8);
  st7789_write_data(data);
}

u16 st7789_read_data() {
#ifdef CPU_SPI

  return 0;
#else
  u16 data;
  LCD_DC_SET;
  sunxi_spi_read(SPI0, &data, 2);
  return data;
#endif
}

void st7789_address_set(u16 x1, u16 y1, u16 x2, u16 y2) {
  if (horizontal_mode == 0) {
    st7789_write_cmd(0x2A);  // Column Address Set
    st7789_write_data16(x1);
    st7789_write_data16(x2);

    st7789_write_cmd(0x2B);  // Page Address Set
    st7789_write_data16(y1);
    st7789_write_data16(y2);
    st7789_write_cmd(0x2c);  // Memory Write
  } else if (horizontal_mode == 1) {
    st7789_write_cmd(0x2A);  // Column Address Set
    st7789_write_data16(x1);
    st7789_write_data16(x2);

    st7789_write_cmd(0x2B);  // Page Address Set
    st7789_write_data16(y1 + 80);
    st7789_write_data16(y2 + 80);
    st7789_write_cmd(0x2c);  // Memory Write
  } else if (horizontal_mode == 2) {
    st7789_write_cmd(0x2A);  // Column Address Set
    st7789_write_data16(x1);
    st7789_write_data16(x2);

    st7789_write_cmd(0x2B);  // Page Address Set
    st7789_write_data16(y1);
    st7789_write_data16(y2);
    st7789_write_cmd(0x2c);  // Memory Write
  } else {
    st7789_write_cmd(0x2A);  // Column Address Set
    st7789_write_data16(x1 + 80);
    st7789_write_data16(x2 + 80);

    st7789_write_cmd(0x2B);  // Page Address Set
    st7789_write_data16(y1);
    st7789_write_data16(y2);
    st7789_write_cmd(0x2c);  // Memory Write
  }
}

void st7789_fill(u16 xsta, u16 ysta, u16 xend, u16 yend, u16 color) {
  u16 i, j;
  st7789_address_set(xsta, ysta, xend - 1, yend - 1);  // 设置显示范围
  for (i = ysta; i < yend; i++) {
    for (j = xsta; j < xend; j++) {
      st7789_write_data16(color);
    }
  }
}

u16 st7789_read_id() {
  u16 id = 0;
  st7789_write_cmd(0x04);
  st7789_read_data();
  id = st7789_read_data();
  st7789_read_data();
  st7789_read_data();

  return id;
}

void st7789_init() {
  kprintf("st7789 init\n");

#ifdef CPU_SPI
  gpio_config(GPIO_C, 0, GPIO_OUTPUT);  // RS/DC     PC0
  gpio_config(GPIO_C, 1, GPIO_OUTPUT);  // SPI_CLK PC1
  gpio_config(GPIO_C, 2, GPIO_OUTPUT);  // SPI_CS  PC2
  gpio_config(GPIO_C, 3, GPIO_OUTPUT);  // SPI_MOSI PC3
  gpio_config(GPIO_B, 2, GPIO_OUTPUT);  // RESET      PB2
#else

  gpio_config(GPIO_C, 0, GPIO_OUTPUT);  // RS/DC     PC0
  gpio_config(GPIO_C, 1, 3);            // SPI_CLK PC1
  gpio_config(GPIO_C, 2, 3);            // SPI_CS  PC2
  gpio_config(GPIO_C, 3, 3);            // SPI_MOSI PC3
  gpio_config(GPIO_B, 2, GPIO_OUTPUT);  // RESET      PB2

#endif

  kprintf("st7789 spi gipio end\n");

  delay(20);

  // init lcd
  st7789_reset();

#ifdef CPU_SPI

#else
  // use SPI0_BASE
  sunxi_spi_set_base(spio_base);
  sunxi_spi_init(SPI0);
  sunxi_spi_cs(SPI0, 0);

  int rate = 240 * 320 * 60;
  rate = 240 * 240 * 60;
  rate = 1000000;

  rate = 12000000 ;//4608000;
  kprintf("st7789 spi rate %d\n", rate);

  sunxi_spi_rate(SPI0, rate);  // 240x320x60

  u16 id = st7789_read_id();
  kprintf("st7789 spi init id %x end\n", id);

  kprintf("st7789 lcd reset\n");

#endif

  //************************************************
  st7789_write_cmd(0x3A);  // 65k mode
  st7789_write_data(0x05);
  st7789_write_cmd(0xC5);  // VCOM
  st7789_write_data(0x1A);
  st7789_write_cmd(0x36);  // 屏幕显示方向设置
  if (horizontal_mode == 0) {
    st7789_write_data(0x00);
  } else if (horizontal_mode == 1) {
    st7789_write_data(0xC0);
  } else if (horizontal_mode == 2) {
    st7789_write_data(0x70);
  } else {
    st7789_write_data(0xA0);
  }

  //-------------ST7789V Frame rate setting-----------//
  st7789_write_cmd(0xb2);  // Porch Setting
  st7789_write_data(0x05);
  st7789_write_data(0x05);
  st7789_write_data(0x00);
  st7789_write_data(0x33);
  st7789_write_data(0x33);
  // st7789_write_data(0x0C);
  // st7789_write_data(0x0C);
  // st7789_write_data(0x00);
  // st7789_write_data(0x33);
  // st7789_write_data(0x33);

  st7789_write_cmd(0xb7);   // Gate Control
  st7789_write_data(0x05);  // 12.2v   -10.43v
  //--------------ST7789V Power setting---------------//
  st7789_write_cmd(0xBB);  // VCOM
  st7789_write_data(0x3F);

  st7789_write_cmd(0xC0);  // Power control
  st7789_write_data(0x2c);

  st7789_write_cmd(0xC2);  // VDV and VRH Command Enable
  st7789_write_data(0x01);

  st7789_write_cmd(0xC3);   // VRH Set
  st7789_write_data(0x0F);  // 4.3+( vcom+vcom offset+vdv)

  st7789_write_cmd(0xC4);   // VDV Set
  st7789_write_data(0x20);  // 0v

  st7789_write_cmd(0xC6);   // Frame Rate Control in Normal Mode
  st7789_write_data(0x0F);  // 0x0F 60Hz 0X01 111Hz

  st7789_write_cmd(0xd0);  // Power Control 1
  st7789_write_data(0xa4);
  st7789_write_data(0xa1);

  st7789_write_cmd(0xE8);  // Power Control 1
  st7789_write_data(0x03);

  st7789_write_cmd(0xE9);  // Equalize time control
  st7789_write_data(0x09);
  st7789_write_data(0x09);
  st7789_write_data(0x08);
  //---------------ST7789V gamma setting-------------//
  st7789_write_cmd(0xE0);  // Set Gamma
  st7789_write_data(0xD0);
  st7789_write_data(0x05);
  st7789_write_data(0x09);
  st7789_write_data(0x09);
  st7789_write_data(0x08);
  st7789_write_data(0x14);
  st7789_write_data(0x28);
  st7789_write_data(0x33);
  st7789_write_data(0x3F);
  st7789_write_data(0x07);
  st7789_write_data(0x13);
  st7789_write_data(0x14);
  st7789_write_data(0x28);
  st7789_write_data(0x30);

  st7789_write_cmd(0XE1);  // Set Gamma
  st7789_write_data(0xD0);
  st7789_write_data(0x05);
  st7789_write_data(0x09);
  st7789_write_data(0x09);
  st7789_write_data(0x08);
  st7789_write_data(0x03);
  st7789_write_data(0x24);
  st7789_write_data(0x32);
  st7789_write_data(0x32);
  st7789_write_data(0x3B);
  st7789_write_data(0x14);
  st7789_write_data(0x13);
  st7789_write_data(0x28);
  st7789_write_data(0x2F);

  st7789_write_cmd(0x20);  // 反显
  st7789_write_cmd(0x11);  // Exit Sleep // 退出睡眠模式
  delay(120);
  st7789_write_cmd(0x29);  // Display on // 开显示

  kprintf("st7789 lcd 1\n");

  st7789_test();

  kprintf("st7789 lcd end\n");
}

void st7789_set_pixel(u32 x, u32 y, u32 color) {
  st7789_address_set(x, y, 1 + x, y + 1);  // 设置光标位置
  st7789_write_data16(color);
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

  // st7789_full(0xFFFFFF);

  // st7789_full(GREEN);

  // kprintf("st7789 test 1\n");

  // st7789_fill(0, 0, 240, 240, 0);
  kprintf("st7789 test 2\n");

  st7789_fill(0, 0, 240, 320, 0X001F);
  st7789_fill(0, 0, 128, 128, GREEN);
  // st7789_fill(0, 0, 128, 128, RED);

  kprintf("st7789 test lcd end\n");
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