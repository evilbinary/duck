/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "gpio.h"
#include "gpio/sunxi-gpio.h"
#include "i2c.h"
#include "kernel/kernel.h"
#include "sunxi-twi.h"

#define TWI0_BASE 0x02502000
#define TWI1_BASE 0x02502400
#define TWI2_BASE 0x02502800
#define TWI3_BASE 0x02502C00

static sunxi_i2c_t *i2c_base[] = {
    (sunxi_i2c_t *)TWI0_BASE,
    (sunxi_i2c_t *)TWI1_BASE,
    (sunxi_i2c_t *)TWI2_BASE,
    (sunxi_i2c_t *)TWI3_BASE,

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

  // De-assert TWI0
  u32 reg = io_read32(CCU_BASE + 0x091C);
  io_write32(CCU_BASE + 0x091C, reg | 1 << 16);

  // gpio set sda  pb7 TWI0_SDA
  gpio_config(GPIO_B, 2, 4);  // 0100: TWI0_SDA
  gpio_pull(GPIO_B, 2, GPIO_PULL_UP);

  // gpio set scl pb6 TWI0_SCK
  gpio_config(GPIO_B, 3, 4);  // 0100: TWI0_SCK
  gpio_pull(GPIO_B, 3, GPIO_PULL_UP);

  // gpio  KEY_IRQ
  gpio_config(GPIO_B, 4, 0xe);  // 1110:PB-EINT4
  

  sunxi_i2c_init(0);

  return 0;
}
