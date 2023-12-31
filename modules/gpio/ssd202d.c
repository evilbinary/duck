/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "ssd202d.h"

#include "gpio.h"
#include "kernel/kernel.h"

// 0x207a00
static ssd202_gpio_t gpio_table[] = {
    __GPIO(0),  __GPIO(1),  __GPIO(2),  __GPIO(3),  __GPIO(4),  __GPIO(5),
    __GPIO(6),  __GPIO(7),  __GPIO(8),  __GPIO(9),  __GPIO(10), __GPIO(11),
    __GPIO(12), __GPIO(13), __GPIO(14), __GPIO(15), __GPIO(16), __GPIO(17),
    __GPIO(18), __GPIO(19), __GPIO(20), __GPIO(21), __GPIO(22), __GPIO(23),
    __GPIO(24), __GPIO(25), __GPIO(26), __GPIO(27), __GPIO(28), __GPIO(29),
    __GPIO(30), __GPIO(31), __GPIO(32), __GPIO(33), __GPIO(34), __GPIO(35),
    __GPIO(36), __GPIO(37), __GPIO(38), __GPIO(39), __GPIO(40), __GPIO(41),
    __GPIO(42), __GPIO(43), __GPIO(44), __GPIO(45), __GPIO(46), __GPIO(47),
    __GPIO(48), __GPIO(49), __GPIO(50), __GPIO(51), __GPIO(52), __GPIO(53),
    __GPIO(54), __GPIO(55), __GPIO(56), __GPIO(57), __GPIO(58), __GPIO(59),
    __GPIO(60), __GPIO(61), __GPIO(62), __GPIO(63), __GPIO(64), __GPIO(65),
    __GPIO(66), __GPIO(67), __GPIO(68), __GPIO(69), __GPIO(70), __GPIO(71),
    __GPIO(72), __GPIO(73), __GPIO(74), __GPIO(75), __GPIO(76), __GPIO(77),
    __GPIO(78), __GPIO(79), __GPIO(80), __GPIO(81), __GPIO(82), __GPIO(83),
    __GPIO(84), __GPIO(85), __GPIO(86), __GPIO(87), __GPIO(88), __GPIO(89),
    __GPIO(90),

};

void gpio_init_device(device_t *dev) {
  log_debug("gpio init device ssd202d\n");
  for (int i = 0; i < 90; i++) {
    log_debug("init start %d\n", i);
    int addr = ADDR_REG8(gpio_table[i].r_oen);

    log_debug("gpio ssd202d map addr %x\n", addr);
    page_map(addr, addr, PAGE_DEV);
  }
  log_debug("gpio init device ssd202d end\n");
}

void gpio_config(u32 gpio, u32 pin, u32 val) {
  // if (pin >= 0 && pin < GPIO_NUMBER) {
  //   REG8(gpio_table[pin].r_oen) |= (gpio_table[pin].m_oen);
  // }

  if (pin >= 0 && pin < GPIO_NUMBER) {
    // log_debug("1 %x\n", ADDR_REG8(gpio_table[pin].r_oen));

    REG8(gpio_table[pin].r_oen) &= (~gpio_table[pin].m_oen);
    if (val) {
      REG8(gpio_table[pin].r_out) |= gpio_table[pin].m_out;
    } else {
      REG8(gpio_table[pin].r_out) &= ~gpio_table[pin].m_out;
    }
    log_debug("4\n");
  }
}

void gpio_pull(u32 gpio, u32 pin, u32 val) {
  if (pin >= 0 && pin < GPIO_NUMBER) {
    if (val) {
      REG8(gpio_table[pin].r_out) |= gpio_table[pin].m_out;
    } else {
      REG8(gpio_table[pin].r_out) &= ~gpio_table[pin].m_out;
    }
  }
}

void gpio_output(u32 gpio, u32 pin, u32 val) {
  if (pin >= 0 && pin < GPIO_NUMBER) {
    REG8(gpio_table[pin].r_oen) &= (~gpio_table[pin].m_oen);
    if (val) {
      REG8(gpio_table[pin].r_out) |= gpio_table[pin].m_out;
    } else {
      REG8(gpio_table[pin].r_out) &= ~gpio_table[pin].m_out;
    }
  }
}

u32 gpio_input(u32 gpio, u32 pin) {
  if (pin >= 0 && pin < GPIO_NUMBER) {
    return ((REG8(gpio_table[pin].r_in) & gpio_table[pin].m_in) ? 1 : 0);
  } else {
    return 0;
  }
}
