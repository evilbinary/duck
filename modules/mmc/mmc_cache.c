/*******************************************************************
 * Copyright 2021-present evilbinary
 * MMC sector cache + sequential read-ahead
 *
 * Fixes in the old CACHE_ENABLED path:
 *  - cached_blocks kmalloc used entry count as bytes (need * sizeof)
 *  - write updated data but not tags → stale/wrong hits
 *  - direct-mapped only: sequential 512B reads never hit
 *
 * Strategy: direct-mapped tagged cache; on miss, multi-block read-ahead
 * into consecutive slots so sequential FatFs 512B reads hit.
 ********************************************************************/
#include "mmc_cache.h"

#include "kernel/kernel.h"

#ifndef MMC_CACHE_ENTRIES
#define MMC_CACHE_ENTRIES 128 /* 128 * 512 = 64KB */
#endif
#ifndef MMC_CACHE_READAHEAD
#define MMC_CACHE_READAHEAD 16 /* 8KB ahead on miss */
#endif

#define MMC_CACHE_MASK (MMC_CACHE_ENTRIES - 1)

static inline u8 *mmc_cache_slot(sdhci_device_t *dev, u32 index) {
  return dev->cache_buffer + (index * BYTE_PER_SECTOR);
}

int mmc_cache_init(sdhci_device_t *dev) {
  u32 i;

  if (dev == NULL) {
    return -1;
  }
  if (dev->cache_buffer != NULL) {
    return 0;
  }

  /* Must be sizeof(uint) * entries — old code allocated only ENTRIES bytes. */
  dev->cached_blocks =
      (uint *)kmalloc(sizeof(uint) * MMC_CACHE_ENTRIES, KERNEL_TYPE);
  dev->cache_buffer =
      (u8 *)kmalloc(BYTE_PER_SECTOR * MMC_CACHE_ENTRIES, KERNEL_TYPE);
  if (dev->cached_blocks == NULL || dev->cache_buffer == NULL) {
    if (dev->cached_blocks) {
      kfree(dev->cached_blocks);
      dev->cached_blocks = NULL;
    }
    if (dev->cache_buffer) {
      kfree(dev->cache_buffer);
      dev->cache_buffer = NULL;
    }
    log_error("mmc_cache_init alloc failed\n");
    return -1;
  }

  for (i = 0; i < MMC_CACHE_ENTRIES; i++) {
    dev->cached_blocks[i] = 0xFFFFFFFFu;
  }
  kmemset(dev->cache_buffer, 0, BYTE_PER_SECTOR * MMC_CACHE_ENTRIES);
  log_info("mmc cache on: %d sectors (%d KB), readahead %d\n",
           MMC_CACHE_ENTRIES,
           (MMC_CACHE_ENTRIES * BYTE_PER_SECTOR) / 1024, MMC_CACHE_READAHEAD);
  return 0;
}

void mmc_cache_invalidate_all(sdhci_device_t *dev) {
  u32 i;
  if (dev == NULL || dev->cached_blocks == NULL) {
    return;
  }
  for (i = 0; i < MMC_CACHE_ENTRIES; i++) {
    dev->cached_blocks[i] = 0xFFFFFFFFu;
  }
}

void mmc_cache_after_write(sdhci_device_t *dev, u32 bno, u32 bcount,
                           const void *data) {
  u32 i;
  if (dev == NULL || dev->cached_blocks == NULL || bcount == 0) {
    return;
  }
  for (i = 0; i < bcount; i++) {
    u32 lba = bno + i;
    u32 idx = lba & MMC_CACHE_MASK;
    if (data != NULL) {
      kmemmove(mmc_cache_slot(dev, idx),
               (const u8 *)data + i * BYTE_PER_SECTOR, BYTE_PER_SECTOR);
      dev->cached_blocks[idx] = lba;
    } else if (dev->cached_blocks[idx] == lba) {
      dev->cached_blocks[idx] = 0xFFFFFFFFu;
    }
  }
}

static int mmc_cache_fill_readahead(sdhci_device_t *dev, u32 bno,
                                   mmc_hw_read_fn hw_read, void *ctx) {
  u32 n = MMC_CACHE_READAHEAD;
  u32 need = n * BYTE_PER_SECTOR;
  u8 *ra;
  int ret;
  u32 i;

  if (dev->read_buf == NULL || dev->read_buf_size < need) {
    if (dev->read_buf) {
      kfree(dev->read_buf);
    }
    dev->read_buf = (u8 *)kmalloc(need, DEVICE_TYPE);
    if (dev->read_buf == NULL) {
      return -1;
    }
    dev->read_buf_size = need;
  }
  ra = dev->read_buf;

  ret = hw_read(ctx, bno, n, ra);
  if (ret < 0) {
    return -1;
  }

  for (i = 0; i < n; i++) {
    u32 lba = bno + i;
    u32 idx = lba & MMC_CACHE_MASK;
    kmemmove(mmc_cache_slot(dev, idx), ra + i * BYTE_PER_SECTOR,
             BYTE_PER_SECTOR);
    dev->cached_blocks[idx] = lba;
  }
  return 0;
}

int mmc_cache_read(sdhci_device_t *dev, u32 bno, u32 boffset, u32 len,
                   void *buf, mmc_hw_read_fn hw_read, void *ctx) {
  u32 remaining;
  u32 sector_off;
  u32 lba;
  u8 *out = (u8 *)buf;

  if (dev == NULL || buf == NULL || len == 0 || hw_read == NULL) {
    return -1;
  }
  if (dev->cache_buffer == NULL || dev->cached_blocks == NULL) {
    return -1;
  }
  if (boffset >= BYTE_PER_SECTOR) {
    return -1;
  }

  remaining = len;
  sector_off = boffset;
  lba = bno;

  while (remaining > 0) {
    u32 idx = lba & MMC_CACHE_MASK;
    u32 copy_len;

    if (dev->cached_blocks[idx] != lba) {
      if (mmc_cache_fill_readahead(dev, lba, hw_read, ctx) < 0) {
        if (dev->read_buf == NULL ||
            dev->read_buf_size < BYTE_PER_SECTOR) {
          if (dev->read_buf) kfree(dev->read_buf);
          dev->read_buf = (u8 *)kmalloc(BYTE_PER_SECTOR, DEVICE_TYPE);
          if (dev->read_buf == NULL) return -1;
          dev->read_buf_size = BYTE_PER_SECTOR;
        }
        if (hw_read(ctx, lba, 1, dev->read_buf) < 0) {
          return -1;
        }
        kmemmove(mmc_cache_slot(dev, idx), dev->read_buf, BYTE_PER_SECTOR);
        dev->cached_blocks[idx] = lba;
      }
    }

    copy_len = BYTE_PER_SECTOR - sector_off;
    if (copy_len > remaining) {
      copy_len = remaining;
    }
    kmemmove(out, mmc_cache_slot(dev, idx) + sector_off, copy_len);
    out += copy_len;
    remaining -= copy_len;
    sector_off = 0;
    lba++;
  }
  return (int)len;
}
