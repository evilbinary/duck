#ifndef __SUNXI_SPI_H__
#define __SUNXI_SPI_H__

#include "kernel/kernel.h"
#include "spi.h"

typedef struct sunxi_spi {
  u32 reserved;   // reserved
  u32 gcr;        // SPI Global Control Register
  u32 tcr;        // SPI Transfer Control register
  u32 reserved0;  // reserved
  u32 ier;        // SPI Interrupt Control register
  u32 isr;        // SPI Interrupt Status register
  u32 fcr;        // SPI FIFO Control register
  u32 fsr;        // SPI FIFO Status register
  u32 wcr;        // SPI Wait Clock Counter register
  u32 ccr;        // SPI Clock Rate Control register
  u32 reserved1;  // reserved
  u32 reserved2;  // reserved
  u32 mbc;        // SPI Burst Counter register
  u32 mtc;        // SPI Transmit Counter Register
  u32 bcc;        // SPI Burst Control register
  u32 reserved3[(0x200 - 0x38) / 4- 1];
  u32 txd;  // SPI TX Data register
  u32 reserved4[(0x100) / 4- 1];
  u32 rxd;  // SPI RX Data register
} sunxi_spi_t;

u32 sunxi_spi_read_write(int spi, u32* data, u32 count);
void sunxi_spi_cs(int spi, u32 val);

#endif