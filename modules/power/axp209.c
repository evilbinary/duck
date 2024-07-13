/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "i2c/i2c.h"
#include "kernel/kernel.h"

/* Chip physical address */
#define AXP209_I2C_ADDR 0x34

/* Chip register adresses */

#define AXP209_REG_0H 0x0  // 电源状态寄存器
#define AXP209_REG_1H 0x1  // 电源模式/充电状态寄存器

#define AXP209_REG_32H 0x32  // 关机、电池检测、CHGLED 控制寄存器
#define AXP209_REG_PEK_PARAMS 0x36  // PEK 参数设置寄存器
#define AXP209_INTERRUPT_BANK_1_ENABLE 0x40
#define AXP209_INTERRUPT_BANK_1_STATUS 0x48
#define AXP209_INTERRUPT_BANK_2_ENABLE 0x41
#define AXP209_INTERRUPT_BANK_2_STATUS 0x49
#define AXP209_INTERRUPT_BANK_3_ENABLE 0x42
#define AXP209_INTERRUPT_BANK_3_STATUS 0x4A
#define AXP209_INTERRUPT_BANK_4_ENABLE 0x43
#define AXP209_INTERRUPT_BANK_4_STATUS 0x4B
#define AXP209_INTERRUPT_BANK_5_ENABLE 0x44
#define AXP209_INTERRUPT_BANK_5_STATUS 0x4C

/* Masks */
#define AXP209_INTERRUPT_PEK_SHORT_PRESS 0x02
#define AXP209_INTERRUPT_PEK_LONG_PRESS 0x01

int axp209_write(u8 cmd, u16 data) {
  int twi = 0;
  char buf[3];
  buf[0] = cmd;
  buf[1] = data & 0xff;
  buf[2] = data >> 8;

  i2c_msg_t msg;
  msg.buf = buf;
  msg.len = 3;
  msg.flags = I2C_WRITE;
  msg.no = 0;
  msg.addr = AXP209_I2C_ADDR;  // AXP209_I2C_ADDR

  sunxi_i2c_start(twi);

  int ret = sunxi_i2c_write_data(twi, &msg);
  sunxi_i2c_stop(twi);
  return ret;
}

u16 axp209_read(u8 reg) {
  int twi = 0;
  char buf[2];

  buf[0] = reg;
  i2c_msg_t msg;
  msg.buf = buf;
  msg.len = 1;
  msg.flags = I2C_WRITE;
  msg.no = 0;
  msg.addr = AXP209_I2C_ADDR;  // AXP209_I2C_ADDR

  sunxi_i2c_start(twi);

  int ret = sunxi_i2c_write_data(twi, &msg);
  // kprintf("pcal read write ret=%x\n", ret);

  buf[0] = 0;
  buf[1] = 0;
  msg.buf = buf;
  msg.len = 2;
  msg.flags = I2C_READ;
  msg.no = 0;
  msg.addr = AXP209_I2C_ADDR;  // AXP209_I2C_ADDR
  sunxi_i2c_start(twi);

  ret = sunxi_i2c_read_data(twi, &msg);
  sunxi_i2c_stop(twi);

  return *((u16*)msg.buf);
}

void axp209_init() {
  log_info("axp209 init\n");

  u16 ret = axp209_read(AXP209_REG_0H);
  kprintf("axp209 0 reg ret =%x\n", ret);
  if (ret & (1 << 7)) {
    kprintf("acin\n");
  } else {
    kprintf("acin not exist \n");
  }
  if (ret & (1 << 5)) {
    kprintf("vbus \n");
  } else {
    kprintf("vbus not exist \n");
  }

  ret = axp209_read(AXP209_REG_1H);
  kprintf("axp209 01 reg ret =%x\n", ret);

  // 电池存在状态指示
  if (ret & (1 << 5)) {
    kprintf("connect bat \n");
  } else {
    kprintf("bat not connect \n");
  }

  // 充电指示
  if (ret & (1 << 6)) {
    kprintf("chargeing\n");
  } else {
    kprintf("finsh charge \n");
  }
}

void power_init_device(device_t* dev) { axp209_init(); }