#ifndef __GPIO_H__
#define __GPIO_H__

#include "kernel/kernel.h"

typedef struct gpio{
  char* name;
  int base;
  int ngpio;
  void* data;
}gpio_t;

/* 【跨平台语义】gpio_config(gpio, pin, val) 的 val = 复用功能/方向：
 *   GPIO_FUNC_INPUT / GPIO_FUNC_OUTPUT 是方向组合（无硬件复用语义的平台
 *   自行解释）；GPIO_FUNC_ALTF0..8 是外设复用编号（RP1 funcsel 与之一致）。
 * gpio_pull 的 val = GPIO_PULL_NONE/DOWN/UP。
 * 【raspi5(RP1) 特例】pin 用全局 GPIO 号 0-53（RP1 是单一控制器，
 * gpio 参数忽略），见 rp1.c。 */
#define GPIO_FUNC_INPUT   0x15 /* DIR_FLAG | RIO(5) */
#define GPIO_FUNC_OUTPUT  0x35 /* DIR_FLAG | RIO(5) | OUT */
#define GPIO_FUNC_ALTF0   0
#define GPIO_FUNC_ALTF1   1
#define GPIO_FUNC_ALTF2   2
#define GPIO_FUNC_ALTF3   3
#define GPIO_FUNC_ALTF4   4
#define GPIO_FUNC_ALTF6   6
#define GPIO_FUNC_ALTF7   7
#define GPIO_FUNC_NONE    9

#define GPIO_PULL_NONE    0
#define GPIO_PULL_DOWN    1
#define GPIO_PULL_UP      2

void gpio_config(u32 gpio, u32 pin, u32 val);
void gpio_pull(u32 gpio, u32 pin, u32 val);
void gpio_output(u32 gpio, u32 pin, u32 val);
u32 gpio_input(u32 gpio, u32 pin);

#endif