#include "sunxi-gpio.h"

static sunxi_gpio_t **gpio_base = NULL;

void gpio_set_base(u32 *base) { gpio_base = base; }

void gpio_config(u32 gpio, u32 pin, u32 val) {
  sunxi_gpio_t *gp = gpio_base[gpio];

  int reg = pin / 8;
  int shift = (pin & 0x7) << 2;
  int tmp;

  tmp = gp->config[reg] & ~(0xf << shift);
  gp->config[reg] = tmp | (val << shift);
}

void gpio_pull(u32 gpio, u32 pin, u32 val) {
  sunxi_gpio_t *gp = gpio_base[gpio];
  int reg = pin / 16;
  int shift = (pin & 0xf) << 1;
  int tmp;

  tmp = gp->pull[reg] & ~(0x3 << shift);
  gp->pull[reg] = tmp | (val << shift);
}

void gpio_drive(u32 gpio, u32 pin, u32 val) {
  sunxi_gpio_t *gp = gpio_base[gpio];
  int reg = pin / 8;
  int shift = (pin & 0x7) << 2;
  int tmp;

  tmp = gp->drive[reg] & ~(0x3 << shift);
  gp->drive[reg] = tmp | (val << shift);
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
}
