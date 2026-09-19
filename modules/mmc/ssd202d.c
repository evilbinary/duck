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

/* ==============================================================================
 * SSD202D(miyoo) SD 数据通路：DMA + 静态页对齐 bounce 缓冲。
 *
 * 为什么不用 CIF(PIO)：实测 CIF 每个 64 字节 region 的首字 bit7 被硬件采错
 * （卡上 42/4d/53/50/46 → 读回 c2/cd/d3/d0/c6），与时钟开关、cache 维护、
 * pre-full 位、MIU 突发位全都无关，软件无法规避，故弃用。
 *
 * 为什么 DMA 要走 bounce：把内核堆指针直接交给 DMA，数据不会落到缓冲区
 * （表现为"两次读互不一致"）。参考实现（ewokos system 侧 sd.c）同样使用
 * 固定页对齐的专用缓冲，DMA 完成后再拷到目的地。
 *
 * cache 维护（语义见 arch/cpu.h）：读前 cpu_flush_dcache_range（clean+invalidate，
 * 丢掉填充留下的 CPU 脏行）；读后 cpu_invalidate_dcache_range（仅失效）——
 * 若误用 flush，clean 会把旧脏行回写、覆盖 DMA 刚写入的数据。
 *
 * 可靠性：双读校验——两个缓冲区分别填 0x00/0xA5（不同填充），任何"半截搬运"
 * 都会留下不同残渣而被发现；一致才采用，脏但一致记 warn，始终不一致报读失败。
 * ============================================================================== */
#define RD_VERIFY_TRIES 3

/* DMA 专用 bounce 缓冲（参考实现 ewokos system sd.c 同款做法：固定页对齐专用
 * 缓冲，DMA 完成后再拷到真正目的地，而不是把内核堆指针交给 DMA 控制器）。 */
static u8 dma_bounce[BYTE_PER_SECTOR] __attribute__((aligned(4096)));

/* 单扇区读。返回：0 = 干净成功；1 = 脏成功（控制器报错但卡干净）；
 * -1 = 真失败（卡 R1 里有错，或超时导致 r1==0）。 */
static int mmc_read_sector(u32 lba, u8 *p, TransEmType mode) {
  u8 *target = p;

  if (mode == EV_DMA) {
    target = dma_bounce;
    kmemset(dma_bounce, 0, BYTE_PER_SECTOR);
    cpu_flush_dcache_range((unsigned long)dma_bounce,
                           (unsigned long)dma_bounce + BYTE_PER_SECTOR);
  }

  RspStruct *rsp =
      HAL_SDMMC_DATAReq(0, 17, lba, 1, BYTE_PER_SECTOR, mode, target);

  if (mode == EV_DMA) {
    cpu_invalidate_dcache_range((unsigned long)dma_bounce,
                                (unsigned long)dma_bounce + BYTE_PER_SECTOR);
    kmemcpy(p, dma_bounce, BYTE_PER_SECTOR);
  }

  if (rsp->eErrCode == 0) {
    return 0;
  }

  {
    u32 r1 = ((u32)rsp->u8ArrRspToken[1] << 24) |
             ((u32)rsp->u8ArrRspToken[2] << 16) |
             ((u32)rsp->u8ArrRspToken[3] << 8) | (u32)rsp->u8ArrRspToken[4];
    u32 st = (r1 >> 9) & 0xF;

    /* 卡侧干净、只有控制器报错：搬运可能不全，标"脏"由上层取舍 */
    if ((r1 & 0xFFF80000u) == 0 && st != 0) {
      return 1;
    }
    log_error("sd read error lba=%d err=0x%04X line=%d r1=%08x st=%d\n", lba,
              (uint16_t)rsp->eErrCode, rsp->u32ErrLine, r1, st);
    return -1;
  }
}

static u32 mmc_read_blocks(sdhci_device_t *hci, u32 start, u32 blkcnt,
                           u8 *buf) {
  static u8 verify_buf[BYTE_PER_SECTOR];
  u32 i;

  for (i = 0; i < blkcnt; i++) {
    u32 lba = start + i;
    u8 *p = buf + i * BYTE_PER_SECTOR;
    int attempt;
    int accepted = 0; /* 0 = 还没有可用结果；1 = 两次都干净且一致；2 = 脏但一致 */

    for (attempt = 0; attempt < RD_VERIFY_TRIES; attempt++) {
      int r1;
      int r2;

      /* 两个缓冲区填不同的值，才能暴露"半截搬运"（cache 维护在 mmc_read_sector
       * 内针对 bounce 缓冲做，这里不再重复） */
      kmemset(p, 0x00, BYTE_PER_SECTOR);
      kmemset(verify_buf, 0xA5, BYTE_PER_SECTOR);

      r1 = mmc_read_sector(lba, p, EV_DMA);
      r2 = mmc_read_sector(lba, verify_buf, EV_DMA);

      if (r1 < 0 || r2 < 0) {
        log_error("sd read failed lba=%d (r1=%d r2=%d)\n", lba, r1, r2);
        return 0;
      }
      if (kmemcmp(p, verify_buf, BYTE_PER_SECTOR) != 0) {
        log_error("sd verify mismatch lba=%d try=%d/%d (dirty=%d/%d)\n", lba,
                  attempt + 1, RD_VERIFY_TRIES, r1, r2);
        continue;
      }
      if (r1 == 0 && r2 == 0) { /* 两次都干净 ⇒ 最可信，直接采用 */
        accepted = 1;
        break;
      }
      if (accepted == 0) { /* 脏但一致：先留着；若之后有干净结果会被覆盖 */
        accepted = 2;
      }
    }

    if (accepted == 0) {
      log_error("sd verify FAILED lba=%d after %d tries (unstable data)\n", lba,
                RD_VERIFY_TRIES);
      /* 返回 0 ⇒ sdhci_dev_port_read 判失败 ⇒ FatFs 报读错：
       * 宁可上层看到错误，也不把可疑数据当成功 ✗ */
      return 0;
    }
    if (accepted == 2) {
      log_error("sd WARN dirty-but-consistent lba=%d\n", lba);
    }
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
  /* 清零范围必须是 bsize 而不是 len：bcount 会向上取整到整扇区，尾部多出来的
   * 那部分如果不清，读失败时会露出上一次的残留数据。 */
  kmemset(sdhci_dev->read_buf, 0, bsize);
  ret = mmc_read_blocks(sdhci_dev, bno, bcount, sdhci_dev->read_buf);
  if (ret != bsize) { /* 任一扇区失败：整次读作废，让上层看到错误而不是脏数据 */
    return 0;
  }
  kmemmove(buf, sdhci_dev->read_buf + boffset, len);

  return ret;
}

int sdhci_dev_port_write(sdhci_device_t *sdhci_dev, char *buf, u32 len) {
  return 0;
}

void sdhci_dev_init(sdhci_device_t *sdhci_dev) {
  log_info("sdhci ssd202d dev init\n");
  // sd mmc0
  page_map(A_FCIE1_0_BANK, A_FCIE1_0_BANK, PAGE_DEV);

  u32 addr = GET_CARD_BANK(0, 0);  // 0x1f282000
  log_debug("addr =>%x\n", addr);
  // page_map(addr, addr, 0);

  /* SDMMC_Init() 内部做 SetBusTiming(EV_BUS_DEF) + SetNrcDelay(8MHz)：
   * 补采样相位与 NRC 配置（此前该函数为空，全靠 boot 阶段遗留）。 */
  SDMMC_Init(0);

#ifdef CACHE_ENABLED
  sdhci_dev->cached_blocks = kmalloc(CACHE_ENTRIES, DEFAULT_TYPE);
  sdhci_dev->cache_buffer = kmalloc(SECTOR_SIZE * CACHE_ENTRIES, DEFAULT_TYPE);
  int i;
  for (i = 0; i < CACHE_ENTRIES; i++) {
    sdhci_dev->cached_blocks[i] = 0xFFFFFFFF;
  }
  kmemset(sdhci_dev->cache_buffer, 0, SECTOR_SIZE * CACHE_ENTRIES);
#endif

  log_info("sdhci ssd202d init end\n");
}
