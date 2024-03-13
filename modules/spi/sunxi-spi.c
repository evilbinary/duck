#include "sunxi-spi.h"

sunxi_spi_t* sunxi_spi_base = 0;

void sunxi_spi_set_base(u32 base) { sunxi_spi_base = base; }

void sunxi_spi_cs(spi_t* spi, u32 val) {
  u32 r = sunxi_spi_base->tcr;
  r &= ~((3 << 4) | (1 << 7));
  r |= (val & 3) << 4 | (0 << 7);
  sunxi_spi_base->tcr = r;
}

u32 sunxi_spi_rw_data(spi_t* spi, spi_msg_t* msg) {
  u32 ret = 0;
  u32 tx_len = msg->tx_len;
  u32 rx_len = msg->rx_len;

  u8* tx_buf = msg->tx_buf;
  u8* rx_buf = msg->rx_buf;
  u32 fifo_byte = 8;
  if (tx_len < fifo_byte) {
    fifo_byte = 1;
  }
  // kprintf("tx_len %d tx buf %x\n", tx_len, tx_buf);
  for (; tx_len > 0;) {
    // set Master Burst Counter
    sunxi_spi_base->mtc = fifo_byte;
    // Master Write Transmit Counter
    sunxi_spi_base->mbc = fifo_byte;
    sunxi_spi_base->bcc = fifo_byte;

    if (tx_buf != NULL) {
      u32 len = fifo_byte;
      // fill data
      for (; len > 0; len--) {
        sunxi_spi_base->txd = *tx_buf++;
      }
      tx_len -= fifo_byte;
    }

    // start trans
    sunxi_spi_base->tcr |= 1 << 31;

    // kprintf("trans %x\n", sunxi_spi_base->isr);
    // wait finish Transfer Completed
    // while ((sunxi_spi_base->isr & (1 << 12)) == 0);
    while ((sunxi_spi_base->fsr & 0xff) < fifo_byte)
      ;
    // kprintf("trans end %d %d\n", rx_len, tx_len);

    // clear flag
    // sunxi_spi_base->isr = 1 << 12;

    if (rx_buf != NULL) {
      u32 len = fifo_byte;
      for (; len > 0; len--) {
        *rx_buf++ = sunxi_spi_base->rxd;
      }
      rx_len -= fifo_byte;
    }
  }

  return msg->tx_len;
}

static void sunxi_spi_write_txbuf(spi_t* spi, u8* buf, int len) {
  int i;
  sunxi_spi_base->mtc = len & 0xffffff;
  sunxi_spi_base->bcc = len & 0xffffff;
  if (buf) {
    for (i = 0; i < len; i++) sunxi_spi_base->txd = *buf++;
  } else {
    for (i = 0; i < len; i++) sunxi_spi_base->txd = 0xff;
  }
}

u32 sunxi_spi_xfer(spi_t* spi, spi_msg_t* msg, u32 cnt) {
  u32 bits = 640;
  u32 len = msg->tx_len;
  int count = len * bits / 8;
  u8* tx = msg->tx_buf;
  u8* rx = msg->rx_buf;
  u8 val;
  int n, i;

  while (count > 0) {
    n = (count <= 64) ? count : 64;
    sunxi_spi_base->mbc = n;
    sunxi_spi_write_txbuf(spi, tx, n);

    sunxi_spi_base->tcr |= (1 << 31);

    while ((sunxi_spi_base->fsr & 0xff) < n)
      ;
    for (i = 0; i < n; i++) {
      val = sunxi_spi_base->rxd;
      if (rx) *rx++ = val;
    }

    if (tx) tx += n;
    count -= n;
  }
  return len;
}

u32 sunxi_spi_read_write(spi_t* spi, u32* data, u32 count) {
  sunxi_spi_xfer(spi, data, count);
  // if (data == NULL || count <= 0) {
  //   return 0;
  // }
  // spi_msg_t* msg = data;
  // if (spi->inited == 0) {
  //   spi->inited = 1;
  // }
  // u32 ret, i;
  // for (i = 0; i < count; i++, msg++) {
  //   if (msg->flags & SPI_READ) {
  //     ret = sunxi_spi_rw_data(spi, msg);
  //   } else if (msg->flags & SPI_WRITE) {
  //     ret = sunxi_spi_rw_data(spi, msg);
  //   } else {
  //     ret = sunxi_spi_rw_data(spi, msg);
  //   }
  //   if (ret < 0) {
  //     kprintf("read write error %x\n", ret);
  //     break;
  //   }
  // }
  // if (spi->inited == 1) {
  //   spi->inited = 0;
  // }
  return count;
}