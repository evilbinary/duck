/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "gpio/v3s.h"

#include "i2c.h"
#include "kernel/kernel.h"
#include "sunxi-twi.h"
#include "v3s-ccu.h"
#include "gpio/sunxi-gpio.h"
#include "gpio.h"

#define TWI0_BASE 0x01C2AC00
#define TWI1_BASE 0x01C2B000
#define TWI2_BASE 0x01C2B400

static sunxi_i2c_t *i2c_base[] = {
    (sunxi_i2c_t *)TWI0_BASE,
    (sunxi_i2c_t *)TWI1_BASE,
    (sunxi_i2c_t *)TWI2_BASE,

};

int i2c_init_device(device_t *dev) {
  i2c_t *i2c = kmalloc(sizeof(i2c_t), DEFAULT_TYPE);
  dev->data = i2c;

  i2c->inited = 0;
  i2c->read = sunxi_i2c_read_write;
  i2c->write = sunxi_i2c_read_write;

  sunxi_i2c_set_base(i2c_base);

  // map io twi
  page_map(TWI0_BASE, TWI0_BASE, 0);

  // De-assert twi
  u32 reg = io_read32(V3S_CCU_BASE + CCU_BUS_SOFT_RST4);
  io_write32(V3S_CCU_BASE + CCU_BUS_SOFT_RST4, reg | 1);

  // gpio set sda  pb7 TWI0_SDA
  gpio_config(GPIO_B, 7, 2);  // 010: TWI0_SDA
  gpio_pull(GPIO_B, 7, GPIO_PULL_UP);

  // gpio set scl pb6 TWI0_SCK
  gpio_config(GPIO_B, 6, 2);  // 010: TWI0_SCK
  gpio_pull(GPIO_B, 6, GPIO_PULL_UP);


  sunxi_i2c_init(0);



  return 0;
}
