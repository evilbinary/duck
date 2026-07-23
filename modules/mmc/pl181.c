/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "pl181.h"

#include "kernel/kernel.h"
#include "sdhci.h"

#define CACHE_COUNT 1
#define SECTOR_SIZE (512 * CACHE_COUNT)
// #define CACHE_ENABLED 1  // have problem

#define CACHE_ENTRIES (1 << 4)          ///< 16 entries
#define CACHE_MASK (CACHE_ENTRIES - 1)  ///< mask 0x0F

// host controller
static inline void cmd(u32 cmd, u32 arg, u32 resp) {
  io_write32(SD_BASE + ARGUMENT, arg);
  io_write32(SD_BASE + COMMAND, (resp << 6) | cmd | 1 << 10);

  // wait complete
  // 6 CmdRespEnd Read Command response received (CRC check passed)
  // 7 CmdSent Read Command sent (no response required)
  // 0 CmdCrcFail Read Command response received (CRC check failed)
  // 2 CmdTimeOut Read Command response timeout
  // while (!(io_read32(SD_BASE + STATUS) & (1 << 7 | 1 << 6 | 1 << 0 | 1 <<
  // 2)))
  //   ;
}

int sd_init(void) {
  io_write32(SD_BASE + POWER, 0xBF);  // power on
  io_write32(SD_BASE + CLOCK, 0xC6);  // default CLK

  // send init command sequence
  cmd(0, 0, MMC_RSP_NONE);               // go idle state
  cmd(55, 0, MMC_RSP_R1);                // ready state
  cmd(41, 0x10000, MMC_RSP_R3);          // argument must not be zero
  cmd(2, 0, MMC_RSP_R2);                 // ask card CID
  cmd(3, SD_RCA, MMC_RSP_R1);            // assign RCA
  cmd(7, SD_RCA, MMC_RSP_R1);            // transfer state: must use RCA
  cmd(16, BYTE_PER_SECTOR, MMC_RSP_R1);  // set data sector length

  // set interrupt MASK0 registers bits = RxFULL(17)|TxEmpty(18)
  io_write32(SD_BASE + MASK0, (1 << 17) | (1 << 18));
  return 0;
}

static u32 mmc_read_blocks(sdhci_device_t *hci, u32 start, u32 blkcnt,
                           u8 *buf) {
  u32 *tmp = buf;
  int len = blkcnt * BYTE_PER_SECTOR;
  int status = -1;

  // timeout count
  io_write32(SD_BASE + DATATIMER, 0xFFFF0000);
  io_write32(SD_BASE + DATALENGTH, BYTE_PER_SECTOR * blkcnt);

  // cmd18 read mutiple block
  cmd(18, start * BYTE_PER_SECTOR, MMC_RSP_R1);
  // cmd(16, blkcnt, MMC_RSP_R1);

  // 2^9=512 | Data transfer enabled |= From card to controller
  // io_write32(SD_BASE + DATACTRL, 1 << 0 | 1 << 1 | 1 << 2 | (9 << 4));
  // io_write32(SD_BASE + DATACTRL, 1 << 0 | 1 << 1 | 1 << 2 | (9 << 4));
  io_write32(SD_BASE + DATACTRL, 0x93);
  do {
    status = io_read32(SD_BASE + STATUS);
    if (status & (1 << 17)) {
      u32 status_err =
          status & (SDI_STA_DCRCFAIL | SDI_STA_DTIMEOUT | SDI_STA_RXOVERR);
      if (!status_err) {  // Receive FIFO full
        for (int i = 0; i < 16; i++) {
          *(tmp) = io_read32(SD_BASE + FIFO);
          tmp++;
          len -= sizeof(u32);
        }

        // int dcount = io_read32(SD_BASE + DATACOUNT);
        // int da = io_read32(SD_BASE + DATALENGTH);
        // log_debug("data==>%d len=%d dcount=%d status %x\n", da, len, dcount,
        //           status);
      }
    }
    io_write32(SD_BASE + STATUS_CLEAR, 0xFFFFFFFF);

  } while (len > 0);

  // cmd 12 stop
  cmd(12, 0, MMC_RSP_R1);

  // clear
  io_write32(SD_BASE + STATUS_CLEAR, 0xFFFFFFFF);

  if (status & (1 << 2)) {
    log_error(" Command response timeout\n");
    return 0;
  } else if (status & (1 << 8)) {
    log_error("Data end (data counter is zero)\n");
    return blkcnt * BYTE_PER_SECTOR;
  } else if (status & (1 << 3)) {
    log_error("Data timeout\n");
    return 0;
  } else if (status & (1 << 1)) {  // DataCrcFail
    log_error("DataCrcFail\n");
    return 0;
  } else if (status & (1 << 5)) {  // Receive FIFO overrun error
    log_error("Receive FIFO overrun error \n");
    return 0;
  }

  return blkcnt * BYTE_PER_SECTOR;
}

int sdhci_dev_port_read(sdhci_device_t *sdhci_dev, char *buf, u32 len) {
  // log_debug("sdhci_dev_port_read %x %d\n",buf,len);

  u32 ret = 0;
  u32 bno = sdhci_dev->offsetl / BYTE_PER_SECTOR;
  u32 boffset = sdhci_dev->offsetl % BYTE_PER_SECTOR;
  u32 bcount = (len + boffset + BYTE_PER_SECTOR - 1) / BYTE_PER_SECTOR;
  u32 bsize = bcount * BYTE_PER_SECTOR;

  if (bsize > sdhci_dev->read_buf_size) {
    kfree(sdhci_dev->read_buf);
    sdhci_dev->read_buf = kmalloc(bsize, DEVICE_TYPE);
    sdhci_dev->read_buf_size = bsize;
  }

#ifdef CACHE_ENABLED
  if (bcount == CACHE_COUNT) {
    int index = bno & CACHE_MASK;
    char *cache_p = (sdhci_dev->cache_buffer + SECTOR_SIZE * index);
    if (sdhci_dev->cached_blocks[index] != bno) {
      ret = mmc_read_blocks(sdhci_dev, bno, bcount, cache_p);
      sdhci_dev->cached_blocks[index] = bno;
    }
    kmemmove(buf, cache_p + boffset, len);
    return ret;
  }
#endif
  kmemset(sdhci_dev->read_buf, 0, len);
  ret = mmc_read_blocks(sdhci_dev, bno, bcount, sdhci_dev->read_buf);
  kmemmove(buf, sdhci_dev->read_buf + boffset, len);

  return ret;
}

int sdhci_dev_port_write(sdhci_device_t *sdhci_dev, char *buf, u32 len) {
  return 0;
}

void sdhci_dev_init(sdhci_device_t *sdhci_dev) {
  log_info("sdhci pl181 dev init\n");
  // sd mmc0
  page_map(SD_BASE, SD_BASE, PAGE_DEV);

#ifdef CACHE_ENABLED
  sdhci_dev->cached_blocks = kmalloc(CACHE_ENTRIES, DEFAULT_TYPE);
  sdhci_dev->cache_buffer = kmalloc(SECTOR_SIZE * CACHE_ENTRIES, DEFAULT_TYPE);
  int i;
  for (i = 0; i < CACHE_ENTRIES; i++) {
    sdhci_dev->cached_blocks[i] = 0xFFFFFFFF;
  }
  kmemset(sdhci_dev->cache_buffer, 0, SECTOR_SIZE * CACHE_ENTRIES);
#endif

  sd_init();

  log_info("sdhci pl181 init end\n");
}
