/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "gpio.h"
#include "sunxi-gpio.h"


static sunxi_gpio_t *gpio_base[] = {
    (sunxi_gpio_t *)0x02000000,
    (sunxi_gpio_t *)0x02000030, /* GPIO_B */
    (sunxi_gpio_t *)0x02000060, /* GPIO_C */
    (sunxi_gpio_t *)0x02000090, /* GPIO_D */
    (sunxi_gpio_t *)0x020000c0, /* GPIO_E */
    (sunxi_gpio_t *)0x020000f0, /* GPIO_F */
    (sunxi_gpio_t *)0x02000120, /* GPIO_G */
};


void gpio_init_device(device_t* dev){

      gpio_set_base(gpio_base);

}