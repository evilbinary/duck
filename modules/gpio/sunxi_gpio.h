#ifndef _SUNXI_GPIO_H__
#define _SUNXI_GPIO_H__

#include "kernel/kernel.h"

// pull config
#define GPIO_PULL_DISABLE (0)   // 00: Pull-up/down disable
#define GPIO_PULL_UP (1)        // 01: Pull-up
#define GPIO_PULL_DOWN (2)      // 10: Pull-down
#define GPIO_PULL_RESERVED (3)  // 11: Reserved

// cfg config
#define GPIO_INPUT (0)    // 000: Input
#define GPIO_OUTPUT (1)   // 001: Output
#define GPIO_DISABLE (7)  // 111: IO Disable

typedef struct sunxi_gpio {
  volatile unsigned long config[4];//config 0-3 n*0x24+0x00  n*0x24+0x04 n*0x24+0x08 n*0x24+0x0C
  volatile unsigned long data;  //data n*0x24+0x10
  volatile unsigned long drive[2]; //driv 0-1
  volatile unsigned long pull[2];  //pul 0-1
}sunxi_gpio_t;

void gpio_set_base(u32 * base);

#endif