
#ifndef __SUNXI_TWI_H
#define __SUNXI_TWI_H

#include "kernel/kernel.h"
#include "i2c.h"


#define I2C_STAT_BUS_ERROR 0x00
#define I2C_STAT_TX_START 0x08
#define I2C_STAT_TX_RSTART 0x10
#define I2C_STAT_TX_AW_ACK 0x18
#define I2C_STAT_TX_AW_NAK 0x20
#define I2C_STAT_TXD_ACK 0x28
#define I2C_STAT_TXD_NAK 0x30
#define I2C_STAT_LOST_ARB 0x38
#define I2C_STAT_TX_AR_ACK 0x40
#define I2C_STAT_TX_AR_NAK 0x48
#define I2C_STAT_RXD_ACK 0x50
#define I2C_STAT_RXD_NAK 0x58
#define I2C_STAT_IDLE 0xf8

#define INT_FLAG (1 << 3)
#define M_STA (1 << 5)
#define M_STP (1 << 4)
#define A_ACK (1 << 2)
#define BUS_EN (1 << 6)

#define kdbg

typedef struct sunxi_i2c {
  u32 addr;   // TWI Slave address
  u32 xaddr;  // TWI Extended slave address
  u32 data;   // TWI Data byte
  u32 cntr;   // TWI Control register
  u32 stat;   // TWI Status register
  u32 ccr;    // TWI Clock control register
  u32 srst;   // TWI Software reset
  u32 efr;    // TWI Enhance Feature register
  u32 lcr;    // TWI Line Control register
} sunxi_i2c_t;

u32 sunxi_i2c_read_write(i2c_t* i2c, u32* data, u32 count) ;


#endif