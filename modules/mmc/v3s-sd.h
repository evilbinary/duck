#ifndef V3S_SD_H
#define V3S_SD_H

#include "sdhci.h"
#include "v3s-reg.h"

#define CACHE_COUNT 1
#define SECTOR_SIZE (512 * CACHE_COUNT)
// #define CACHE_ENABLED 1  // have problem

#define CACHE_ENTRIES (1 << 4)          ///< 16 entries
#define CACHE_MASK (CACHE_ENTRIES - 1)  ///< mask 0x0F


void sd_init (void);
void sd_deinit (void);
int sd_card_detect (void);
int sd_card_init (void);
int sd_read (void *ptr, u32 addr, u32 cnt);
int sd_write (void *ptr, u32 addr, u32 cnt);

#endif