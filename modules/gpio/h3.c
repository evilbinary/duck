#include "gpio.h"
#include "sunxi-gpio.h"


static sunxi_gpio_t *gpio_base[] = {
    (sunxi_gpio_t *)0x01C20800, /* GPIO_A */
    (sunxi_gpio_t *)0x01C20824, /* GPIO_B */
    (sunxi_gpio_t *)0x01C20848, /* GPIO_C */
    (sunxi_gpio_t *)0x01C2086C, /* GPIO_D */
    (sunxi_gpio_t *)0x01C20890, /* GPIO_E */
    (sunxi_gpio_t *)0x01C208B4, /* GPIO_F */
    (sunxi_gpio_t *)0x01C208D8, /* GPIO_G */
    (sunxi_gpio_t *)0x01C208FC, /* GPIO_H */
    (sunxi_gpio_t *)0x01C20920, /* GPIO_I */
    (sunxi_gpio_t *)0x01F02c00, /* GPIO_L */
};




void gpio_init_device(device_t* dev){

  gpio_set_base(gpio_base);

}