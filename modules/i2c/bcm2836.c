/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "i2c.h"
#include "kernel/kernel.h"

int i2c_init_device(device_t* dev) {
  i2c_t* i2c = kmalloc(sizeof(i2c_t),DEFAULT_TYPE);
  dev->data = i2c;

  i2c->inited = 0;

  return 0;
}
