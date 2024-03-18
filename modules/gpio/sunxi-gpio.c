#include "sunxi-gpio.h"

#define log_debug

static sunxi_gpio_t **gpio_base = NULL;

void gpio_set_base(u32 *base) { gpio_base = base; }

void gpio_config(u32 gpio, u32 pin, u32 val) {
  if (gpio_base == NULL) {
    log_error("gpio not init\n");
    return;
  }
  sunxi_gpio_t *gp = gpio_base[gpio];

  int reg = pin / 8;
  int shift = (pin & 0x7) << 2;
  int tmp;

  tmp = gp->config[reg] & ~(0xf << shift);
  gp->config[reg] = tmp | (val << shift);

  log_debug("gpio config %d pin %d %x shift %d value %x\n", gpio, pin,
            &gp->config[reg], shift, gp->config[reg]);
}

void gpio_pull(u32 gpio, u32 pin, u32 val) {
  if (gpio_base == NULL) {
    log_error("gpio not init\n");
    return;
  }

  sunxi_gpio_t *gp = gpio_base[gpio];
  int reg = pin / 16;
  int shift = (pin & 0xf) << 1;
  int tmp;

  tmp = gp->pull[reg] & ~(0x3 << shift);
  gp->pull[reg] = tmp | (val << shift);

  log_debug("gpio pull %d pin %d %x shift %d value %x\n", gpio, pin,
            &gp->pull[reg], shift, gp->pull[reg]);
}

void gpio_drive(u32 gpio, u32 pin, u32 val) {
  if (gpio_base == NULL) {
    log_error("gpio not init\n");
    return;
  }
  sunxi_gpio_t *gp = gpio_base[gpio];
  int reg = pin / 8;
  int shift = (pin & 0x7) << 2;
  int tmp;

  tmp = gp->drive[reg] & ~(0x3 << shift);
  gp->drive[reg] = tmp | (val << shift);

  log_debug("gpio drive %d pin %d %x shift %d value %x\n", gpio, pin,
            &gp->drive[reg], shift, gp->drive[reg]);
}

void gpio_output(u32 gpio, u32 pin, u32 val) {
  if (gpio_base == NULL) {
    log_error("gpio not init\n");
    return;
  }
  sunxi_gpio_t *gp = gpio_base[gpio];
  if (val) {
    gp->data |= 1 << pin;
  } else {
    gp->data &= ~(1 << pin);
  }
  log_debug("gpio output %d pin %d %x shift %d value %x\n", gpio, pin,
            &gp->data, pin, gp->data);
}

u32 gpio_input(u32 gpio, u32 pin) {
  if (gpio_base == NULL) {
    log_error("gpio not init\n");
    return;
  }
  sunxi_gpio_t *gp = gpio_base[gpio];
  return (gp->data >> pin) & 1;
}
