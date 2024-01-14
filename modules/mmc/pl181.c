/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "kernel/kernel.h"
#include "sdhci.h"
#include "ssd202d_sdmmc.h"

#define CACHE_COUNT 1
#define SECTOR_SIZE (512 * CACHE_COUNT)
// #define CACHE_ENABLED 1  // have problem

#define CACHE_ENTRIES (1 << 4)          ///< 16 entries
#define CACHE_MASK (CACHE_ENTRIES - 1)  ///< mask 0x0F

static u32 mmc_read_blocks(sdhci_device_t *hci, u32 start, u32 blkcnt,
                           u8 *buf) {
  // RspStruct *pst_rsp;
  // int u8Slot = 0;

  // u16 u8CMD = 17;
  // if (blkcnt > 1) {
  //   u8CMD = 18;
  // }
  // pst_rsp =
  //     HAL_SDMMC_DATAReq(u8Slot, u8CMD, start, blkcnt, BYTE_PER_SECTOR, EV_DMA,
  //                       buf);  // CMD17
  // if (pst_rsp->eErrCode != 0) {
  //   log_error("log read block error start %x count %d=> (Err: 0x%04X)\n", start,
  //             blkcnt, (uint16_t)pst_rsp->eErrCode);
  // }
  // if (blkcnt > 1) {
  //   pst_rsp = HAL_SDMMC_CMDReq(u8Slot, 12, 0x00000000, EV_R1B);  // CMD12;
  // }

  // log_debug("start %x count %d=> (Err: 0x%04X)\n", start, blkcnt,
  //           (uint16_t)pst_rsp->eErrCode);
  return blkcnt * BYTE_PER_SECTOR;
}

int sdhci_dev_port_read(sdhci_device_t *sdhci_dev, char *buf, u32 len) {
  // log_debug("sdhci_dev_port_read %x %d\n",buf,len);

  u32 ret = 0;
//   u32 bno = sdhci_dev->offsetl / BYTE_PER_SECTOR;
//   u32 boffset = sdhci_dev->offsetl % BYTE_PER_SECTOR;
//   u32 bcount = (len + boffset + BYTE_PER_SECTOR - 1) / BYTE_PER_SECTOR;
//   u32 bsize = bcount * BYTE_PER_SECTOR;

//   if (bsize > sdhci_dev->read_buf_size) {
//     kfree(sdhci_dev->read_buf);
//     sdhci_dev->read_buf = kmalloc(bsize, DEVICE_TYPE);
//     sdhci_dev->read_buf_size = bsize;
//   }

// #ifdef CACHE_ENABLED
//   if (bcount == CACHE_COUNT) {
//     int index = bno & CACHE_MASK;
//     char *cache_p = (sdhci_dev->cache_buffer + SECTOR_SIZE * index);
//     if (sdhci_dev->cached_blocks[index] != bno) {
//       ret = mmc_read_blocks(sdhci_dev, bno, bcount, cache_p);
//       sdhci_dev->cached_blocks[index] = bno;
//     }
//     kmemmove(buf, cache_p + boffset, len);
//     return ret;
//   }
// #endif
//   kmemset(sdhci_dev->read_buf, 0, len);
//   ret = mmc_read_blocks(sdhci_dev, bno, bcount, sdhci_dev->read_buf);
//   kmemmove(buf, sdhci_dev->read_buf + boffset, len);

  return ret;
}

int sdhci_dev_port_write(sdhci_device_t *sdhci_dev, char *buf, u32 len) {
  return 0;
}

void sdhci_dev_init(sdhci_device_t *sdhci_dev) {
  log_info("sdhci ssd202d dev init\n");
//   // sd mmc0
//   page_map(A_FCIE1_0_BANK, A_FCIE1_0_BANK, L2_NCNB);

//   u32 addr = GET_CARD_BANK(0, 0);  // 0x1f282000
//   log_debug("addr =>%x\n", addr);
//   // page_map(addr, addr, 0);

//   SDMMC_Init(0);

// #ifdef CACHE_ENABLED
//   sdhci_dev->cached_blocks = kmalloc(CACHE_ENTRIES, DEFAULT_TYPE);
//   sdhci_dev->cache_buffer = kmalloc(SECTOR_SIZE * CACHE_ENTRIES, DEFAULT_TYPE);
//   int i;
//   for (i = 0; i < CACHE_ENTRIES; i++) {
//     sdhci_dev->cached_blocks[i] = 0xFFFFFFFF;
//   }
//   kmemset(sdhci_dev->cache_buffer, 0, SECTOR_SIZE * CACHE_ENTRIES);
// #endif

  log_info("sdhci ssd202d init end\n");
}
