
/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "sdhci.h"
#include "sunxi-sdhci.h"
#include "gpio.h"

#define CACHE_COUNT 1
#define SECTOR_SIZE (512 * CACHE_COUNT)
// #define CACHE_ENABLED 1  // have problem

#define CACHE_ENTRIES (1 << 4)          ///< 16 entries
#define CACHE_MASK (CACHE_ENTRIES - 1)  ///< mask 0x0F

static void print_hex(u8 *addr, u32 size) {
  for (int x = 0; x < size; x++) {
    kprintf("%02x ", addr[x]);
    if (x != 0 && (x % 32) == 0) {
      kprintf("\n");
    }
  }
  kprintf("\n\r");
}

int sdhci_dev_port_read(sdhci_device_t *sdhci_dev, char *buf, u32 len) {
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
  u32 ret = 0;
  u32 bno = sdhci_dev->offsetl / BYTE_PER_SECTOR;
  u32 boffset = sdhci_dev->offsetl % BYTE_PER_SECTOR;
  u32 bcount = (len + boffset + BYTE_PER_SECTOR - 1) / BYTE_PER_SECTOR;
  u32 bsize = bcount * BYTE_PER_SECTOR;

  if (bsize > sdhci_dev->write_buf_size) {
    kfree(sdhci_dev->write_buf);
    sdhci_dev->write_buf = kmalloc(bsize, DEVICE_TYPE);
    sdhci_dev->write_buf_size = bsize;
  }

  ret = mmc_write_blocks(sdhci_dev, buf, bno, bcount);

#ifdef CACHE_ENABLED
  int i;
  u8 *p = buf;
  for (i = 0; i < bcount; i++) {
    int index = (bno + i) & CACHE_MASK;
    void *cache_p = (void *)(sdhci_dev->cache_buffer + SECTOR_SIZE * index);
    kmemmove(cache_p, &p[SECTOR_SIZE * i], SECTOR_SIZE);
  }
#endif

  return ret;
}

void sdhci_dev_init(sdhci_device_t *sdhci_dev) {
  sdhci_sunxi_pdata_t *pdat =
      kmalloc(sizeof(sdhci_sunxi_pdata_t), DEFAULT_TYPE);
  sdhci_dev->data = pdat;
  pdat->high_capacity = 0;
  pdat->read_bl_len = BYTE_PER_SECTOR;
  pdat->rca = 0;
  pdat->isspi = 0;
  pdat->write_bl_len = BYTE_PER_SECTOR;
  pdat->virt = SD_BASE;
  pdat->port = 0;

  pdat->voltage = MMC_VDD_27_36;
  pdat->clock = 400 * 1000;
  pdat->width = MMC_BUS_WIDTH_4;

  pdat->reset = 8;
  pdat->clk = 162;
  pdat->clkcfg = 2;
  pdat->cmd = 2;
  pdat->cmdcfg = 2;
  pdat->dat0 = 161;
  pdat->dat0cfg = 2;
  pdat->dat1 = 160;
  pdat->dat1cfg = 2;
  pdat->dat2 = 165;
  pdat->dat2cfg = 2;
  pdat->dat3 = 164;
  pdat->dat3cfg = 2;
  pdat->dat4 = -1;
  pdat->dat4cfg = -1;
  pdat->dat5 = -1;
  pdat->dat5cfg = -1;
  pdat->dat6 = -1;
  pdat->dat6cfg = -1;
  pdat->dat7 = -1;
  pdat->dat7cfg = -1;
  pdat->cd = 166;
  pdat->cdcfg = 0;

  // sd mmc0
  page_map(SD_BASE, SD_BASE, 0);

  sdhci_sunxi_probe(sdhci_dev);

#ifdef CACHE_ENABLED
  sdhci_dev->cached_blocks = kmalloc(CACHE_ENTRIES, DEFAULT_TYPE);
  sdhci_dev->cache_buffer = kmalloc(SECTOR_SIZE * CACHE_ENTRIES, DEFAULT_TYPE);
  int i;
  for (i = 0; i < CACHE_ENTRIES; i++) {
    sdhci_dev->cached_blocks[i] = 0xFFFFFFFF;
  }
  kmemset(sdhci_dev->cache_buffer, 0, SECTOR_SIZE * CACHE_ENTRIES);
#endif

  kprintf("sdh dev init end\n");
}
