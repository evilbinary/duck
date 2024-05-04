#include "sunxi-spi.h"

sunxi_spi_t** sunxi_spi_base = NULL;

#define SUNXI_SPI_MAX_RATE (24 * 1000 * 1000)
#define SUNXI_SPI_MIN_RATE (3 * 1000)

void sunxi_spi_set_base(u32* base) { sunxi_spi_base = base; }

void sunxi_spi_cs(int spi, u32 val) {
  u32 r = sunxi_spi_base[spi]->tcr;
  r &= ~((3 << 4) | (1 << 7));
  r |= ((val & 3) << 4) | (1 << 7);
  sunxi_spi_base[spi]->tcr = r;
}

void sunxi_spi_rate(int spi, u32 rate) {
  rate = min(rate, (unsigned int)SUNXI_SPI_MAX_RATE);
  rate = max((unsigned int)SUNXI_SPI_MIN_RATE, rate);

  u32 div = SUNXI_SPI_MAX_RATE / (2 * rate);
  u32 reg = 0;
  if (div <= (0xff + 1)) {
    if (div > 0) {
      div--;
    }
    // Source_CLK / (2*(n + 1)).
    reg = (div & 0xff) | (1 << 12);
  } else {
    div = ilog2(SUNXI_SPI_MAX_RATE) - ilog2(rate);
    reg = (div & 0xf) << 8;
  }
  sunxi_spi_base[spi]->ccr = reg;

  log_debug("spi %d rate=%u\n", spi, rate);
}

void sunxi_spi_set_mode(int spi, u32 mode) {
  u32 reg = sunxi_spi_base[spi]->tcr;
  reg &= ~(0x3 << 0);
  reg |= (mode & 0x3) << 0;

  sunxi_spi_base[spi]->tcr = reg;
}

static void sunxi_spi_write_txbuf(int spi, u8* buf, int len) {
  int i;

  sunxi_spi_base[spi]->mtc = len & 0xffffff;
  sunxi_spi_base[spi]->bcc = len & 0xffffff;
  sunxi_spi_base[spi]->mbc = len & 0xffffff;
  if (buf) {
    for (i = 0; i < len; i++) {
      io_write8(&sunxi_spi_base[spi]->txd, *buf);
      // kprintf("trans txd=%x d=%x\n",io_read8(&sunxi_spi_base[spi]->txd),
      // *buf);
      buf++;
    }
  } else {
    for (i = 0; i < len; i++) {
      io_write8(&sunxi_spi_base[spi]->txd, 0xff);
    }
  }
}

u32 sunxi_spi_xfer(int spi, spi_msg_t* msg) {
  if (spi > 1 || spi < 0) {
    log_error("spi num error %d\n", spi);
    return -1;
  }

  int count = msg->tx_len * msg->bits / 8;
  u8* tx = msg->tx_buf;
  u8* rx = msg->rx_buf;
  u8 val;
  int n, i;

  while (count > 0) {
    n = (count <= 64) ? count : 64;
    sunxi_spi_base[spi]->mbc = n;
    sunxi_spi_write_txbuf(spi, tx, n);

    sunxi_spi_base[spi]->tcr |= (1 << 31);

    // while (sunxi_spi_base[spi]->tcr & (1 << 31));
    while ((sunxi_spi_base[spi]->fsr & 0xff) < n);

    for (i = 0; i < n; i++) {
      val = sunxi_spi_base[spi]->rxd;
      if (rx) *rx++ = val;
    }
    // sunxi_spi_base[spi]->isr = 1 << 12;
    if (tx) tx += n;
    count -= n;
  }
  if (!tx) {
    return msg->rx_len;
  } else {
    return msg->tx_len;
  }
}

u32 sunxi_spi_rw_data(int spi, spi_msg_t* msg) {
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
    sunxi_spi_base[spi]->mtc = fifo_byte;
    // Master Write Transmit Counter
    sunxi_spi_base[spi]->mbc = fifo_byte;
    sunxi_spi_base[spi]->bcc = 1<<28| fifo_byte<<24|fifo_byte;

    if (tx_buf != NULL) {
      u32 len = fifo_byte;
      // fill data
      for (; len > 0; len--) {
        // u8 d = *tx_buf;
        sunxi_spi_base[spi]->txd = *tx_buf++;

        // kprintf("trans txd=%x d=%x\n", sunxi_spi_base[spi]->txd, d);
      }
      tx_len -= fifo_byte;
    }

    // start trans
    sunxi_spi_base[spi]->tcr |= 1 << 31;

    kprintf("trans %x\n", sunxi_spi_base[spi]->isr);
    // wait finish Transfer Completed
    while ((sunxi_spi_base[spi]->isr & (1 << 12)) == 0);
    while ((sunxi_spi_base[spi]->fsr & 0xff) < fifo_byte);
    kprintf("trans end %d %d\n", rx_len, tx_len);

    // clear flag
    sunxi_spi_base[spi]->isr = 1 << 12;

    if (rx_buf != NULL) {
      u32 len = fifo_byte;
      for (; len > 0; len--) {
        *rx_buf++ = sunxi_spi_base[spi]->rxd;
      }
      rx_len -= fifo_byte;
    }
  }

  return msg->tx_len;
}

u32 sunxi_spi_read(int spi, u32* data, u32 size) {
  spi_msg_t msg = {0};
  msg.rx_buf = data;
  msg.rx_len = size;
  msg.bits = 8;
  msg.size = size;
  sunxi_spi_xfer(spi, &msg);
}

u32 sunxi_spi_write(int spi, u32* data, u32 size) {
  spi_msg_t msg = {0};
  msg.tx_buf = data;
  msg.tx_len = size;
  msg.bits = 8;
  sunxi_spi_xfer(spi, &msg);

  // sunxi_spi_rw_data(spi,&msg);
}
