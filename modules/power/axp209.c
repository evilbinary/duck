/*******************************************************************
* Copyright 2021-present evilbinary
* 作者: evilbinary on 01/01/20
* 邮箱: rootdebug@163.com
********************************************************************/
#include "kernel/kernel.h"


/* Chip physical address */
#define AXP209_I2C_ADDR                         0x34

/* Chip register adresses */
#define AXP209_REG_32H                          0x32
#define AXP209_REG_PEK_PARAMS                   0x36
#define AXP209_INTERRUPT_BANK_1_ENABLE          0x40
#define AXP209_INTERRUPT_BANK_1_STATUS          0x48
#define AXP209_INTERRUPT_BANK_2_ENABLE          0x41
#define AXP209_INTERRUPT_BANK_2_STATUS          0x49
#define AXP209_INTERRUPT_BANK_3_ENABLE          0x42
#define AXP209_INTERRUPT_BANK_3_STATUS          0x4A
#define AXP209_INTERRUPT_BANK_4_ENABLE          0x43
#define AXP209_INTERRUPT_BANK_4_STATUS          0x4B
#define AXP209_INTERRUPT_BANK_5_ENABLE          0x44
#define AXP209_INTERRUPT_BANK_5_STATUS          0x4C

/* Masks */
#define AXP209_INTERRUPT_PEK_SHORT_PRESS        0x02
#define AXP209_INTERRUPT_PEK_LONG_PRESS         0x01



void axp209_init(){

}