/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "gpio/v3s.h"

#include "gpio/sunxi-gpio.h"
#include "kernel/kernel.h"
#include "spi.h"
#include "sunxi-spi.h"
#include "v3s-ccu.h"

#define SPI0_BASE 0x01C68000
#define SPI1_BASE 0x01C68000

static sunxi_spi_t* spio_base[] = {
    (sunxi_spi_t*)SPI0_BASE,  // spi 0
    (sunxi_spi_t*)SPI1_BASE,  // spi 1
};

void sunxi_spi_init(int spi) {
  if (spi == 0) {
    // map io spi0
    page_map(SPI0_BASE, SPI0_BASE, 0);

    // gpio set miso pc0 SPI_MISO
    gpio_config(GPIO_C, 0, 3);  // 011: SPI0_MISO
    gpio_pull(GPIO_C, 0, GPIO_PULL_DISABLE);

    // gpio set sck pc1 SPI_SCK
    gpio_config(GPIO_C, 1, 3);  // 011: SPI0_CLK
    gpio_pull(GPIO_C, 1, GPIO_PULL_DISABLE);

    // gpio set cs pc2 SPI_CS
    gpio_config(GPIO_C, 2, 3);  // 011: SPI0_CS
    gpio_pull(GPIO_C, 2, GPIO_PULL_DISABLE);

    // gpio set mosi pc3 SPI_MOSI
    gpio_config(GPIO_C, 3, 3);  // 011: SPI0_MOSI
    gpio_pull(GPIO_C, 3, GPIO_PULL_DISABLE);

    

    u32 reg = 0;

    //spi0 bus gate, deassert spi
    reg = io_read32(V3S_CCU_BASE + CCU_BUS_SOFT_RST0);
    io_write32(V3S_CCU_BASE + CCU_BUS_SOFT_RST0, reg | 1 << 20);

    // set spi0 clock bus gating
    reg = io_read32(V3S_CCU_BASE + CCU_BUS_CLK_GATE0);
    io_write32(V3S_CCU_BASE + CCU_BUS_CLK_GATE0, reg | 1 << 20);  // spi 0 gate

    // set spi0 sclk enable
    reg = io_read32(V3S_CCU_BASE + CCU_SPI0_CLK);
    io_write32(V3S_CCU_BASE + CCU_SPI0_CLK, reg | 1 << 31);

  

    // set master mode  stop transmit data when RXFIFO full
    spio_base[spi]->gcr |= 1 << 31 | 1 << 7 | 1 << 1 | 1;

    // wait
    while (spio_base[spi]->gcr & (1 << 31));
    kprintf("spi0 gcr %x\n", spio_base[spi]->gcr);

    // set SS Output Owner Select  1: Active low polarity (1 = Idle)
    // spio_base[spi]->tcr |=  1 << 6 | 1 << 2;
    // spio_base[spi]->tcr |=  1 << 6 | 0 << 2;
    // spio_base[spi]->tcr |= 0 << 6 | 1 << 2;
    spio_base[spi]->tcr |=  0 << 6 | 0 << 2;

    // clear intterrupt
    spio_base[spi]->isr = ~0;

    // set fcr TX FIFO Reset RX FIFO Reset
    spio_base[spi]->fcr |= 1 << 31 | 1 << 15;

    // set sclk clock  Select Clock Divide Rate 2 SPI_CLK = Source_CLK / (2*(n +
    // 1)).
    spio_base[spi]->ccr |= 1 << 12 | 14;

  } else if (spi == 1) {
  }
}

// #define kdbg

// POINT_COLOR
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
#define BROWN 0XBC40
#define BRRED 0XFC07
#define GRAY 0X8430
#define GRAY175 0XAD75
#define GRAY151 0X94B2
#define GRAY187 0XBDD7
#define GRAY240 0XF79E

#define SPI0 0

void lcd_write_data(u8 data) {
  // cs -> dc
  // gpio_pull(GPIO_C, 2, GPIO_PULL_UP);
  // sunxi_spi_cs(gspi, 1);
  lcd_set_dc(1);
  spi_msg_t msg;
  msg.tx_buf = &data;
  msg.tx_len = 1;
  sunxi_spi_write(SPI0, &msg, 1);
}

void lcd_write_data_word(u16 data) {
  // cs -> dc
  // gpio_pull(GPIO_C, 2, GPIO_PULL_UP);
  // sunxi_spi_cs(gspi, 1);
  lcd_set_dc(1);
  spi_msg_t msg = {0};
  msg.tx_buf = &data;
  msg.tx_len = 2;
  sunxi_spi_write(SPI0, &msg, 1);
}

void lcd_write_cmd(u8 cmd) {
  // gpio_pull(GPIO_C, 2, GPIO_PULL_DOWN);
  // sunxi_spi_cs(SPI0, 0);
  lcd_set_dc(0);
  spi_msg_t msg = {0};
  msg.tx_buf = &cmd;
  msg.tx_len = 1;
  sunxi_spi_write(SPI0, &msg, 1);
}

void delay(void) {
  int i;
  for (i = 0; i < 100000; i++) {
  }
}

void lcd_address_set(u16 x1, u16 y1, u16 x2, u16 y2) {
  lcd_write_cmd(0x2a);
  lcd_write_data_word(x1);
  lcd_write_data_word(x2);

  lcd_write_cmd(0x2b);
  lcd_write_data_word(y1);
  lcd_write_data_word(y2);
  lcd_write_cmd(0x2C);
}

void lcd_fill(u16 xsta, u16 ysta, u16 xend, u16 yend, u16 color) {
  u16 i, j;
  lcd_address_set(xsta, ysta, xend - 1, yend - 1);  // 设置显示范围
  for (i = ysta; i < yend; i++) {
    for (j = xsta; j < xend; j++) {
      lcd_write_data(color);
    }
  }
}

void lcd_set_dc(u32 val) { gpio_output(GPIO_G, 0, val); }

void v3s_test_lcd(spi_t* spi) {
  gpio_config(GPIO_G, 0, GPIO_OUTPUT);

  lcd_write_cmd(0x11);
  delay();

  // set horiztional
  lcd_write_cmd(0x36);
  lcd_write_data(0x00);

  //
  lcd_write_cmd(0x3A);
  lcd_write_data(0x05);

  lcd_write_cmd(0xB2);
  lcd_write_data(0x0C);
  lcd_write_data(0x0C);
  lcd_write_data(0x00);
  lcd_write_data(0x33);
  lcd_write_data(0x33);

  lcd_write_cmd(0xB7);
  lcd_write_data(0x35);

  lcd_write_cmd(0xBB);
  lcd_write_data(0x19);

  lcd_write_cmd(0xC0);
  lcd_write_data(0x2C);

  lcd_write_cmd(0xC2);
  lcd_write_data(0x01);

  lcd_write_cmd(0xC3);
  lcd_write_data(0x12);

  lcd_write_cmd(0xC4);
  lcd_write_data(0x20);

  lcd_write_cmd(0xC6);
  lcd_write_data(0x0F);

  lcd_write_cmd(0xD0);
  lcd_write_data(0xA4);
  lcd_write_data(0xA1);

  lcd_write_cmd(0xE0);
  lcd_write_data(0xD0);
  lcd_write_data(0x04);
  lcd_write_data(0x0D);
  lcd_write_data(0x11);
  lcd_write_data(0x13);
  lcd_write_data(0x2B);
  lcd_write_data(0x3F);
  lcd_write_data(0x54);
  lcd_write_data(0x4C);
  lcd_write_data(0x18);
  lcd_write_data(0x0D);
  lcd_write_data(0x0B);
  lcd_write_data(0x1F);
  lcd_write_data(0x23);

  lcd_write_cmd(0xE1);
  lcd_write_data(0xD0);
  lcd_write_data(0x04);
  lcd_write_data(0x0C);
  lcd_write_data(0x11);
  lcd_write_data(0x13);
  lcd_write_data(0x2C);
  lcd_write_data(0x3F);
  lcd_write_data(0x44);
  lcd_write_data(0x51);
  lcd_write_data(0x2F);
  lcd_write_data(0x1F);
  lcd_write_data(0x1F);
  lcd_write_data(0x20);
  lcd_write_data(0x23);
  lcd_write_cmd(0x21);

  lcd_write_cmd(0x29);

  log_debug("v3s test lcd end\n");
  lcd_fill(0, 0, 240, 240, RED);
}

int spi_init_device(device_t* dev) {
  spi_t* spi = kmalloc(sizeof(spi_t), DEFAULT_TYPE);
  dev->data = spi;

  spi->inited = 0;
  // spi->read = sunxi_spi_read;
  // spi->write = sunxi_spi_write;
  spi->cs = sunxi_spi_cs;

  // use SPI0_BASE
  sunxi_spi_set_base(spio_base);

  sunxi_spi_init(0);

  sunxi_spi_cs(0, 1);

  // v3s_test_lcd(spi);

  log_debug("v3s spi init end \n");

  return 0;
}
