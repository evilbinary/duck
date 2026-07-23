/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "gpio/sunxi-gpio.h"
#include "kernel/kernel.h"
#include "spi.h"
#include "sunxi-spi.h"
#include "t113-ccu.h"

static sunxi_spi_t *spio_base[] = {
    (sunxi_spi_t *)0x04025000,  // spi 0
    (sunxi_spi_t *)0x04026000,  // spi 1
};

static void t113_spi_enable_chip(int spi) {
  u32 val;

  val = spio_base[spi]->gcr;
  val |= (1 << 31) | (1 << 7) | (1 << 1) | (1 << 0);
  spio_base[spi]->gcr = val;
  while (spio_base[spi]->gcr & (1 << 31))
    ;

  val = spio_base[spi]->tcr;
  val |= (1 << 6) | (1 << 2);
  spio_base[spi]->tcr = val;

  val = spio_base[spi]->fcr;
  val |= (1 << 31) | (1 << 15);
  spio_base[spi]->fcr = val;
}

void sunxi_spi_init(int spi) {
  log_debug("sunxi_spi_init %d\n", spi);

  // map io spi0
  page_map(spio_base[spi], spio_base[spi], 0);
  page_map(spio_base[spi] + 0x1000, spio_base[spi] + 0x1000, 0);

  if (spi == 0) {
  } else if (spi == 1) {
    // config spi pin 0100:SPI1-WP/DBI-TE
    gpio_config(GPIO_D, 15, 4);
    gpio_config(GPIO_D, 14, 4);
    gpio_config(GPIO_D, 13, 4);
    gpio_config(GPIO_D, 12, 4);
    gpio_config(GPIO_D, 11, 4);
    gpio_config(GPIO_D, 10, 4);

    // SPI1_CLK_REG
    io_write32(T113_CCU_BASE + 0x0944, 1 << 31);

    // SPI_BGR_REG
    io_write32(T113_CCU_BASE + 0x096c, 1 << 17);  // SPI1 Reset

    u32 val = io_read32(T113_CCU_BASE + 0x096c);
    val |= 1 << 1;  // Gating Clock for SPI1
    io_write32(T113_CCU_BASE + 0x096c, val);

    // config spi smaple mode
    spio_base[spi]->tcr |= 0 << 11 | 1 << 13;

    // config spi mode
    spio_base[spi]->tcr |= 0 << 0 | 1 << 1;

    // SS_SEL
    //sunxi_spi_cs(spi,1);
    t113_spi_enable_chip(spi);
  }

  log_debug("sunxi_spi_init end\n");
}

int spi_init_device(device_t *dev) {
  spi_t *spi = kmalloc(sizeof(spi_t), DEFAULT_TYPE);
  dev->data = spi;

  spi->inited = 0;

  // spi->read = sunxi_spi_read_write;
  // spi->write = sunxi_spi_read_write;
  spi->cs = sunxi_spi_cs;

  // use SPI0_BASE
  sunxi_spi_set_base(spio_base);

  sunxi_spi_init(0);

  return 0;
}
