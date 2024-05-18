/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "sunxi-twi.h"

#include "gpio/gpio.h"
#include "i2c.h"
#include "kernel/kernel.h"

// use TWI n
static sunxi_i2c_t** sunxi_i2c_base = NULL;

void sunxi_i2c_set_base(u32* base) { sunxi_i2c_base = base; }

void sunxi_i2c_init(int twi) {
  // Soft Reset TWI
  sunxi_i2c_base[twi]->srst = 1 << 0;
  sunxi_i2c_base[twi]->srst = 0 << 0;

  // set clock  CLK_N = 2, CLK_M=2 400kHz
  // set clock 100Khz, N=1, M=1, APB1 sourced from 24Mhz OSC
  u32 m = 2;
  u32 n = 2;
  sunxi_i2c_base[twi]->ccr = ((m & 0xf) << 3) | ((n & 0x7) << 0);

  // clear slave addr
  sunxi_i2c_base[twi]->addr = 0;
  sunxi_i2c_base[twi]->xaddr = 0;

  // TWI Bus Enable
  sunxi_i2c_base[twi]->cntr = BUS_EN | M_STP;
}

u32 sunxi_i2c_wait(int twi, u32 flag) {
  u32 timeout = 2000;
  for (; timeout > 0; timeout--) {
    u32 cntr = sunxi_i2c_base[twi]->cntr;
    // kprintf("wait cntr %x flag %x\n",cntr,flag);
    if (cntr & flag) {
      // kprintf("wait ok\n");
      return sunxi_i2c_base[twi]->stat;
    }
  }
  // kprintf("wait long %x\n", sunxi_i2c_base[twi]->stat);
  return -1;
}

u32 sunxi_i2c_start(int twi) {
  u32 ret;
  u32 val = sunxi_i2c_base[twi]->cntr;
  val |= INT_FLAG | M_STA;  // Master Mode Stop
  sunxi_i2c_base[twi]->cntr = val;
  ret = sunxi_i2c_wait(twi, M_STA);
  kdbg;
  ret = sunxi_i2c_wait(twi, INT_FLAG);
  // kprintf("wait start %x\n", ret);
  return ret;
}

u32 sunxi_i2c_stop(int twi) {
  u32 ret;
  u32 val = sunxi_i2c_base[twi]->cntr;
  val |= M_STP | INT_FLAG;
  sunxi_i2c_base[twi]->cntr = val;
  kdbg;
  ret = sunxi_i2c_wait(twi, M_STP);
  // kprintf("wait stop %x\n", ret);
  kdbg;
  return sunxi_i2c_wait(twi, INT_FLAG);
}

u32 sunxi_i2c_send_data(int twi, u32 data) {
  sunxi_i2c_base[twi]->data = data;
  sunxi_i2c_base[twi]->cntr = sunxi_i2c_base[twi]->cntr | INT_FLAG;  // INT_FLAG
  kdbg;
  return sunxi_i2c_wait(twi, INT_FLAG);
}

u32 sunxi_i2c_recv_data(int twi) {
  u32 val = 0;
  u32 ret = sunxi_i2c_send_data(twi, val);
  if (ret != I2C_STAT_TX_AR_ACK) {
    return -1;
  }
  sunxi_i2c_base[twi]->cntr |= A_ACK;
  sunxi_i2c_base[twi]->cntr |= ~A_ACK | INT_FLAG;
  kdbg;
  sunxi_i2c_wait(twi, INT_FLAG);
  val = sunxi_i2c_base[twi]->data;
  return val;
}

u32 sunxi_i2c_read_data(int twi, i2c_msg_t* msg) {
  u32 len = msg->len;
  u8* buf = msg->buf;
  u8 addr = msg->addr;

  u32 ret = sunxi_i2c_send_data(twi, (u8)(addr << 1 | 1));
  if (ret != I2C_STAT_TX_AR_ACK) {
    log_error("send data error %x\n", ret);
    return -1;
  }
  sunxi_i2c_base[twi]->cntr |= A_ACK;
  u32 i;
  for (i = 0; len > 0; len--) {
    if (len == 1) {
      sunxi_i2c_base[twi]->cntr =
          (sunxi_i2c_base[twi]->cntr & ~A_ACK) | INT_FLAG;
      kdbg;
      ret = sunxi_i2c_wait(twi, INT_FLAG);
      if (ret != I2C_STAT_RXD_NAK) {
        log_error("rxd ack nak error %x\n", ret);
        return -1;
      }
      break;
    } else {
      sunxi_i2c_base[twi]->cntr |= INT_FLAG;
      kdbg;
      ret = sunxi_i2c_wait(twi, INT_FLAG);
      if (ret != I2C_STAT_RXD_ACK) {
        log_error("rxd ack error %x\n", ret);
        return -1;
      }
    }
    buf[i] = sunxi_i2c_base[twi]->data;
    // kprintf("buf %x\n", (u32)buf[i]);
    i++;
  }
  return msg->len;
}

u32 sunxi_i2c_write_data(int twi, i2c_msg_t* msg) {
  u32 ret = 0;
  u32 len = msg->len;
  u8* buf = msg->buf;
  ret = sunxi_i2c_send_data(twi, (u8)(msg->addr << 1));
  if (ret != I2C_STAT_TX_AW_ACK) {
    log_error(" send data error %x\n", ret);
    return -1;
  }
  for (; len > 0; len--) {
    ret = sunxi_i2c_send_data(twi, *buf++);
    if (ret != I2C_STAT_TXD_ACK) {
      log_error(" send data error %x\n", ret);
      return -1;
    }
  }
  return msg->len;
}

u32 sunxi_i2c_read_write(i2c_t* i2c, u32* data, u32 count) {
  if (data == NULL || count <= 0) {
    return 0;
  }

  i2c_msg_t* pmsg = data;
  int twi = pmsg->no;

  if (i2c->inited == 0) {
    u32 ret = sunxi_i2c_start(twi);
    if (ret != I2C_STAT_TX_START) {
      // log_error("start error\n");
      return -1;
    }
    i2c->inited = 1;
  }
  u32 ret, i;
  for (i = 0; i < count; i++, pmsg++) {
    if (i != 0) {
      if (sunxi_i2c_start(twi) != I2C_STAT_TX_RSTART) {
        log_error("start2 error\n");
        break;
      }
    }
    if (pmsg->flags & I2C_READ) {
      ret = sunxi_i2c_read_data(i2c, pmsg);
    } else if (pmsg->flags & I2C_WRITE) {
      ret = sunxi_i2c_write_data(i2c, pmsg);
    } else {
      ret = sunxi_i2c_write_data(i2c, pmsg);
    }
    if (ret < 0) {
      log_error("read write error %x\n", ret);
      break;
    }
  }
  if (i2c->inited == 1) {
    sunxi_i2c_stop(twi);
    i2c->inited = 0;
  }
  return count;
}
