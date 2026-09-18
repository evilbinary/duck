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

#define AXP209_REG_ADC_5A 0x5A  // VBUS 电压 ADC 数据高 8 位
#define AXP209_REG_ADC_5B 0x5B  // VBUS 电压 ADC 数据低 4 位

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

/* 【带状态检查的寄存器读】成功返回 0，失败返回 -1（i2c 传输没完成 / 从设备不
 * 应答 / 总线死）。原实现把 sunxi_i2c_* 的返回值直接丢掉，失败时也照样返回 buf
 * 里的残留值，调用方分不清"读到 0"和"根本没读到"；失败路径还漏了 stop，会把
 * 控制器留在传输中间，导致后面每次读也跟着失败。 */
static int axp209_read_reg(u8 reg, u16* out) {
  int twi = 0;
  u8 buf[2];
  i2c_msg_t msg;

  buf[0] = reg;
  msg.buf = buf;
  msg.len = 1;
  msg.flags = I2C_WRITE;
  msg.no = 0;
  msg.addr = AXP209_I2C_ADDR;  // AXP209_I2C_ADDR

  sunxi_i2c_start(twi);

  if (sunxi_i2c_write_data(twi, &msg) != (u32)msg.len) {
    sunxi_i2c_stop(twi);
    return -1;
  }

  buf[0] = 0;
  buf[1] = 0;
  msg.buf = buf;
  msg.len = 2;
  msg.flags = I2C_READ;
  msg.no = 0;
  msg.addr = AXP209_I2C_ADDR;  // AXP209_I2C_ADDR
  sunxi_i2c_start(twi);

  if (sunxi_i2c_read_data(twi, &msg) != (u32)msg.len) {
    sunxi_i2c_stop(twi);
    return -1;
  }
  sunxi_i2c_stop(twi);

  /* 低字节在前，与原实现 *((u16*)buf) 等价 */
  *out = (u16)(buf[0] | (buf[1] << 8));
  return 0;
}

u16 axp209_read(u8 reg) {
  u16 val = 0;
  axp209_read_reg(reg, &val);
  return val;
}

void axp209_init() {
  u16 ret = 0;

  /* 【先探测，芯片不存在就不要初始化】AXP209 是可选的 PMIC：不少 V3s/T113 板子
   * 上根本没这颗芯片（或总线上没有从设备）。原实现不分青红皂白读 5 个寄存器，
   * 每次失败都由 i2c 层各打一条 error（V3s 实机刷屏：
   * "write/read data send data error ffffffff"），而且失败时读回的全 0 还会被
   * 当成 "acin/vbus/电池都不存在" 的结论 —— 那是假数据。
   * 这里先用一次带状态检查的读做探测，失败就一行说明后返回：电源/电池/ADC
   * 的后续读取全部跳过，也不再把总线故障包装成"电源状态"。 */
  if (axp209_read_reg(AXP209_REG_0H, &ret) != 0) {
    log_info("axp209 not found (i2c addr 0x%x), skip\n", AXP209_I2C_ADDR);
    return;
  }

  log_info("axp209 init\n");
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

  if (axp209_read_reg(AXP209_REG_1H, &ret) != 0) {
    log_error("axp209 reg 1h read failed, stop\n");
    return;
  }
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

  u16 hight = 0;
  if (axp209_read_reg(AXP209_REG_ADC_5A, &hight) != 0) {
    log_error("axp209 reg 5ah read failed, stop\n");
    return;
  }
  kprintf("axp209 0x5a reg ret =%x\n", hight);

  u16 low = 0;
  if (axp209_read_reg(AXP209_REG_ADC_5B, &low) != 0) {
    log_error("axp209 reg 5bh read failed, stop\n");
    return;
  }
  kprintf("axp209 0x5b reg ret =%x\n", low);

  kprintf("voltage vbus %d\n", (hight & 0xff) << 8 | (low & 0xf));

}

void power_init_device(device_t* dev) { axp209_init(); }