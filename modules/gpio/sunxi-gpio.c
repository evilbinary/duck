#include "sunxi-gpio.h"

#define GPIOn_CFG_ADDR(n) (0x02000000 + (n) * 0x24 + 0x00)
#define GPIOn_DATA_ADDR(n) (0x02000000 + (n) * 0x24 + 0x10)
#define GPIOn_DRV_ADDR(n) (0x02000000 + (n) * 0x24 + 0x14)
#define GPIOn_PUL_ADDR(n) (0x02000000 + (n) * 0x24 + 0x1C)

static sunxi_gpio_t **gpio_base = NULL;

void gpio_set_base(u32 *base) { gpio_base = base; }

void gpio_config(u32 gpio, u32 pin, u32 val) {
  sunxi_gpio_t *gp = gpio_base[gpio];

  int reg = pin / 8;
  int shift = (pin & 0x7) << 2;
  int tmp;

  tmp = gp->config[reg] & ~(0xf << shift);
  gp->config[reg] = tmp | (val << shift);

  // u32 addr;
  // u32 offset;
  // u32 data;

  // if (val & 0x8) {
  //   log_warn("[line]:%d There is a warning with parameter input", __LINE__);
  //   return;
  // }

  // addr = GPIOn_CFG_ADDR(gpio) + (pin / 8) * 4;
  // offset = (pin % 8) * 4;

  // data = io_read32(addr);
  // data &= ~(0x7 << offset);
  // data |= val << offset;
  // log_debug("config gpio %x %x", data, addr);

  // io_write32(addr, data);
}

void gpio_pull(u32 gpio, u32 pin, u32 val) {
  sunxi_gpio_t *gp = gpio_base[gpio];
  int reg = pin / 16;
  int shift = (pin & 0xf) << 1;
  int tmp;

  tmp = gp->pull[reg] & ~(0x3 << shift);
  gp->pull[reg] = tmp | (val << shift);

  // u32 addr;
  // u32 offset;
  // u32 data;

  // addr = GPIOn_PUL_ADDR(gpio);
  // addr += pin > 15 ? 0x4 : 0x0;
  // offset = (pin & 0xf) << 1;

  // data = io_read32(addr);
  // data &= ~(0x3 << offset);
  // data |= val << offset;
  // io_write32(addr, data);
}

void gpio_drive(u32 gpio, u32 pin, u32 val) {
  sunxi_gpio_t *gp = gpio_base[gpio];
  int reg = pin / 8;
  int shift = (pin & 0x7) << 2;
  int tmp;

  tmp = gp->drive[reg] & ~(0x3 << shift);
  gp->drive[reg] = tmp | (val << shift);

  // u32 addr;
  // u32 offset;
  // u32 data;

  // addr = GPIOn_DRV_ADDR(gpio);
  // addr += pin > 15 ? 0x4 : 0x0;
  // offset = (pin & 0xf) << 1;

  // data = io_read32(addr);
  // data &= ~(0x3 << offset);
  // data |= val << offset;
  // io_write32(addr, data);

}

void gpio_output(u32 gpio, u32 pin, u32 val) {
  sunxi_gpio_t *gp = gpio_base[gpio];
  if (val) {
    gp->data |= 1 << pin;
  } else {
    gp->data &= ~(1 << pin);
  }
}

u32 gpio_input(u32 gpio, u32 pin) {
  sunxi_gpio_t *gp = gpio_base[gpio];
  return (gp->data >> pin) & 1;

  // u32 addr;
  // u32 offset;
  // u32 data;

  // addr = GPIOn_DATA_ADDR(gpio);
  // offset = pin;

  // data = io_read32(addr);
  // return (data >> offset) & 0x01;
}
