/*******************************************************************
 * Copyright 2021-present evilbinary
 * Shared MMC sector cache API
 ********************************************************************/
#ifndef MMC_CACHE_H
#define MMC_CACHE_H

#include "sdhci.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Hardware read: read `nsec` sectors starting at `lba` into `buf`.
 * Return >= 0 on success, < 0 on failure.
 */
typedef int (*mmc_hw_read_fn)(void *ctx, u32 lba, u32 nsec, void *buf);

int mmc_cache_init(sdhci_device_t *dev);
void mmc_cache_invalidate_all(sdhci_device_t *dev);

/**
 * Cached read of `len` bytes starting at sector `bno` + `boffset`.
 * Returns len on success, <0 on error.
 */
int mmc_cache_read(sdhci_device_t *dev, u32 bno, u32 boffset, u32 len,
                   void *buf, mmc_hw_read_fn hw_read, void *ctx);

/**
 * After writing `bcount` sectors from `bno`:
 *  - if data != NULL, update cache tags+content
 *  - if data == NULL, invalidate matching tags only
 */
void mmc_cache_after_write(sdhci_device_t *dev, u32 bno, u32 bcount,
                           const void *data);

#ifdef __cplusplus
}
#endif

#endif /* MMC_CACHE_H */
