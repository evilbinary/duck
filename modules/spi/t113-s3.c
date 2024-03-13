/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "kernel/kernel.h"
#include "spi.h"
#include "sunxi-spi.h"

#define SPI0_BASE 0x04025000

int spi_init_device(device_t* dev) {
  spi_t* spi = kmalloc(sizeof(spi_t), DEFAULT_TYPE);
  dev->data = spi;

  spi->inited = 0;

  spi->read = sunxi_spi_read_write;
  spi->write = sunxi_spi_read_write;
  spi->cs = sunxi_spi_cs;

  // use SPI0_BASE
  sunxi_spi_set_base(SPI0_BASE);

  // map io spi0
  page_map(SPI0_BASE, SPI0_BASE, 0);

  return 0;
}
