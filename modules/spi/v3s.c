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
    gpio_drive(GPIO_C, 0, 3);

    // gpio set sck pc1 SPI_SCK
    gpio_config(GPIO_C, 1, 3);  // 011: SPI0_CLK
    gpio_pull(GPIO_C, 1, GPIO_PULL_DISABLE);
    gpio_drive(GPIO_C, 1, 3);

    // gpio set cs pc2 SPI_CS
    gpio_config(GPIO_C, 2, 3);  // 011: SPI0_CS
    gpio_pull(GPIO_C, 2, GPIO_PULL_DOWN);
    gpio_drive(GPIO_C, 2, 3);

    // gpio set mosi pc3 SPI_MOSI
    gpio_config(GPIO_C, 3, 3);  // 011: SPI0_MOSI
    gpio_pull(GPIO_C, 3, GPIO_PULL_DISABLE);
    gpio_drive(GPIO_C, 3, 3);

    u32 reg = 0;

    // spi0 bus gate, deassert spi
    reg = io_read32(V3S_CCU_BASE + CCU_BUS_SOFT_RST0);
    io_write32(V3S_CCU_BASE + CCU_BUS_SOFT_RST0, reg | 1 << 20);

    // set spi0 clock bus gating
    reg = io_read32(V3S_CCU_BASE + CCU_BUS_CLK_GATE0);
    reg |= 1 << 20;                                     // SPI0_GATING
    io_write32(V3S_CCU_BASE + CCU_BUS_CLK_GATE0, reg);  // spi 0 gate

    // set spi0 sclk enable  pll periph0 - 600MHZ SCLK = Clock Source/Divider
    // N/Divider M.
    reg = io_read32(V3S_CCU_BASE + CCU_SPI0_CLK);
    reg = 0;
    reg |= 1 << 31;  // SCLK_GATING SCLK = Clock Source/N/M 600MHZ/2/3= 100MHZ
    reg |= 1 << 24;  // CLK_SRC_SEL 01: PLL_PERIPH0 600MHZ 0 24MHZ
    reg |= 1 << 16;  // CLK_DIV_RATIO_N  01: 2
    reg |= 0 << 0;   // CLK_DIV_RATIO_M
    io_write32(V3S_CCU_BASE + CCU_SPI0_CLK, reg);

    //  set sclk clock  Select Clock Divide Rate 2 SPI_CLK = src_clk / (2*(n +
    //  1)).
    spio_base[spi]->ccr = 0;
    spio_base[spi]->ccr |= 1 << 12;
    spio_base[spi]->ccr &= ~0xFF;
    spio_base[spi]->ccr |= 0;  // 100 MHZ/(2*1)=50

    // enable clock
    reg = io_read32(V3S_CCU_BASE + CCU_SPI0_CLK);
    reg |= 1 << 31;
    io_write32(V3S_CCU_BASE + CCU_SPI0_CLK, reg);

    // set master mode  stop transmit data when RXFIFO full
    spio_base[spi]->gcr |= 1 << 31 | 1 << 1 | 1;  //

    // wait
    while (spio_base[spi]->gcr & (1 << 31));
    kprintf("spi0 gcr %x\n", spio_base[spi]->gcr);

    // set SS Output Owner Select  1: Active low polarity (1 = Idle)
    // spio_base[spi]->tcr |=  1 << 6 | 1 << 2;
    // spio_base[spi]->tcr |=  1 << 6 | 0 << 2;
    // spio_base[spi]->tcr |= 0 << 6 | 1 << 2;
    // spio_base[spi]->tcr |= 0 << 6 | 0 << 2;

    // MSB first CPOL 1 CPHA 1
    // spio_base[spi]->tcr |= 0 << 12; //
    // spio_base[spi]->tcr &= ~(1 << 1); //CPOL 0
    // spio_base[spi]->tcr &= ~(1 << 0); //CPHA 0

    // spio_base[spi]->tcr &= ~(1 << 7); //SS_LEVEL 0

    // spio_base[spi]->tcr |= (1 << 6);

    spio_base[spi]->tcr |= (0 << 1);
    spio_base[spi]->tcr |= (1 << 0);

    spio_base[spi]->tcr |= (1 << 3);
    spio_base[spi]->tcr |= (1 << 6);
    // spio_base[spi]->tcr |= (1 << 2);

    spio_base[spi]->tcr |= (1 << 13);
    // spio_base[spi]->tcr |= (1 << 11);
    spio_base[spi]->tcr |= (1 << 10);
    spio_base[spi]->tcr |= (1 << 8);

    // clear intterrupt
    spio_base[spi]->isr = ~0;

    // set fcr TX FIFO Reset RX FIFO Reset  TF_ DRQ_EN
    spio_base[spi]->fcr |= 1 << 31;
    spio_base[spi]->fcr |= 1 << 15;
    spio_base[spi]->fcr |= 1 << 24;  //| 1<<30  TF_TEST_ENB
    spio_base[spi]->fcr |= 8 << 16;
    spio_base[spi]->fcr |= 1 << 9;
    spio_base[spi]->fcr |= 1 << 14;

    spio_base[spi]->ier |=
        1 << 12 | 1 << 6 | 1 << 5 | 1 << 4 | 1 << 2 | 1 << 1 | 1 << 0;

    // spio_base[spi]->wcr = 2;

  } else if (spi == 1) {
  }
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
