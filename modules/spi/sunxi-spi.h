#ifndef __SUNXI_SPI_H__
#define __SUNXI_SPI_H__

#include "kernel/kernel.h"
#include "spi.h"

typedef struct sunxi_spi {
  u32 reserved;   // reserved
  u32 gcr;        // 0x04 SPI Global Control Register
  u32 tcr;        // 0x08 SPI Transfer Control register
  u32 reserved0;  // 0x0C reserved
  u32 ier;        // 0x10 SPI Interrupt Control register
  u32 isr;        // 0x14 SPI Interrupt Status register
  u32 fcr;        // 0x18 SPI FIFO Control register
  u32 fsr;        // 0x1C SPI FIFO Status register
  u32 wcr;        // 0x20 SPI Wait Clock Counter register
  u32 ccr;        // 0x24 SPI Clock Rate Control register
  u32 reserved1;  // reserved
  u32 reserved2;  // reserved
  u32 mbc;        // 0x30 SPI Burst Counter register
  u32 mtc;        // 0x34 SPI Transmit Counter Register
  u32 bcc;        // 0x38 SPI Burst Control register
  u32 reserved3[(0x200 - 0x38) / 4 - 1];
  u32 txd;  // 0x200 SPI TX Data register
  u32 reserved4[(0x100) / 4 - 1];
  u32 rxd;  // 0x300 SPI RX Data register
} sunxi_spi_t;

u32 sunxi_spi_read_write(int spi, u32* data, u32 count);
void sunxi_spi_cs(int spi, u32 val);

#endif