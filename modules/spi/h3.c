/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "spi.h"
#include "kernel/kernel.h"

int spi_init_device(device_t* dev) {
  spi_t* spi = kmalloc(sizeof(spi_t),DEFAULT_TYPE);
  dev->data = spi;

  spi->inited = 0;
  
  return 0;
}
