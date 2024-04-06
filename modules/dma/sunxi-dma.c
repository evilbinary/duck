/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "sunxi-dma.h"

#include "dma.h"
#include "kernel/kernel.h"

#define SUNXI_DMA_MAX 16

static int dma_init_ok = -1;
static dma_source_t dma_channel_source[SUNXI_DMA_MAX];

// static dma_desc_t dma_channel_desc[SUNXI_DMA_MAX]
// __attribute__((aligned(64)));

dma_desc_t *dma_channel_desc = NULL;

void dma_init_all(void) {
  int i;
  dma_reg_t *const dma_reg = (dma_reg_t *)DMA_BASE;
  if (dma_init_ok > 0) return;

  kprintf("dma init\n");
  page_map(DMA_BASE, DMA_BASE, 0);

  kprintf("ccu base %x\n", ccu);

  /* dma : mbus clock gating */
  ccu->mbus_gate |= 1 << 0;  // DMA_MCLK_EN

  kprintf("ccu bus gate %x\n", ccu->mbus_gate);

  /* dma reset */
  ccu->dma_gate_reset |= 1 << DMA_RST_OFS;

  /* dma gating */
  ccu->dma_gate_reset |= 1 << DMA_GATING_OFS;

  kprintf("ccu gate reset %x\n", ccu->dma_gate_reset);

  dma_reg->irq_en0 = 0;
  dma_reg->irq_en1 = 0;

  dma_reg->irq_pending0 = 0xffffffff;
  dma_reg->irq_pending1 = 0xffffffff;

  /* auto MCLK gating  disable */
  dma_reg->auto_gate &= ~(0x7 << 0);
  dma_reg->auto_gate |= 0x7 << 0;

  kprintf("dma_reg auto_gate %x\n", dma_reg->auto_gate);

  memset((void *)dma_channel_source, 0, SUNXI_DMA_MAX * sizeof(dma_source_t));

  dma_channel_desc =
      kmalloc_alignment(sizeof(dma_desc_t) * SUNXI_DMA_MAX, 64, DEVICE_TYPE);

  for (i = 0; i < SUNXI_DMA_MAX; i++) {
    dma_channel_source[i].used = 0;
    dma_channel_source[i].channel = &(dma_reg->channel[i]);
    dma_channel_source[i].desc = &dma_channel_desc[i];

    kprintf("dma %d channel %x desc %x\n", i, dma_channel_source[i].channel,
            dma_channel_source[i].desc);
  }

  dma_init_ok = 1;

  kprintf("dma init end\n");
}

void dma_exit(void) {
  int i;
  dma_reg_t *dma_reg = (dma_reg_t *)DMA_BASE;
#if defined(CONFIG_SUNXI_VERSION1)
  struct sunxi_ccu_reg *const ccu = (struct sunxi_ccu_reg *)CCU_BASE;
#endif
  /* free dma channel if other module not free it */
  for (i = 0; i < SUNXI_DMA_MAX; i++) {
    if (dma_channel_source[i].used == 1) {
      dma_channel_source[i].channel->enable = 0;
      dma_channel_source[i].used = 0;
    }
  }

#if defined(CONFIG_SUNXI_VERSION1)
  ccu->ahb_gate0 &= ~(1 << AHB_GATE_OFFSET_DMA);
#else
  /* close dma clock when dma exit */
  dma_reg->auto_gate &= ~(1 << DMA_GATING_OFS | 1 << DMA_RST_OFS);
#endif

  dma_init_ok--;
}

u32 dma_request_from_last(u32 dmatype) {
  int i;

  for (i = SUNXI_DMA_MAX - 1; i >= 0; i--) {
    if (dma_channel_source[i].used == 0) {
      dma_channel_source[i].used = 1;
      dma_channel_source[i].channel_count = i;
      return (u32)&dma_channel_source[i];
    }
  }

  return 0;
}

u32 dma_request(u32 dmatype) {
  int i;

  for (i = 0; i < SUNXI_DMA_MAX; i++) {
    if (dma_channel_source[i].used == 0) {
      dma_channel_source[i].used = 1;
      dma_channel_source[i].channel_count = i;
      kprintf("DMA: USE CHANNEL %u\r\n", i);
      return (u32)&dma_channel_source[i];
    }
  }

  return 0;
}

int dma_release(u32 hdma) {
  dma_source_t *dma_source = (dma_source_t *)hdma;

  if (!dma_source->used) return -1;

  dma_source->used = 0;

  return 0;
}

int dma_setting(u32 hdma, dma_set_t *cfg) {
  u32 commit_para;
  dma_set_t *dma_set = cfg;
  dma_source_t *dma_source = (dma_source_t *)hdma;
  dma_desc_t *desc = dma_source->desc;
  u32 channel_addr = (u32)(&(dma_set->channel_cfg));

  if (!dma_source->used) return -1;

  if (dma_set->loop_mode) {
    desc->link = (u32)(&dma_source->desc);
  } else {
    desc->link = SUNXI_DMA_LINK_NULL;
  }

  commit_para = (dma_set->wait_cyc & 0xff);
  commit_para |= (dma_set->data_block_size & 0xff) << 8;

  desc->commit_para = commit_para;
  desc->config = *(volatile u32 *)channel_addr;

  return 0;
}

int dma_start(u32 hdma, u32 saddr, u32 daddr, u32 bytes) {
  dma_source_t *dma_source = (dma_source_t *)hdma;
  dma_channel_reg_t *channel = dma_source->channel;
  dma_desc_t *desc = dma_source->desc;

  if (!dma_source->used) return -1;

  kprintf("dma desc %x channel %x\n", desc, channel);

  /*config desc */
  desc->source_addr = saddr;
  desc->dest_addr = daddr;
  desc->byte_count = bytes;

  /* start dma */
  dmb();
  channel->desc_addr = (u32)desc;
  dmb();
  channel->enable = 1;
  channel->pause = 0;
  dmb();
  kprintf("dma src %x des %x count %d\n", desc->source_addr, desc->dest_addr,
          desc->byte_count);
  return 0;
}

int dma_stop(u32 hdma) {
  dma_source_t *dma_source = (dma_source_t *)hdma;
  dma_channel_reg_t *channel = dma_source->channel;

  if (!dma_source->used) {
    return -1;
  }
  channel->enable = 0;

  return 0;
}

int dma_querystatus(u32 hdma) {
  u32 channel_count;
  dma_source_t *dma_source = (dma_source_t *)hdma;
  dma_reg_t *dma_reg = (dma_reg_t *)DMA_BASE;

  if (!dma_source->used) {
    return -1;
  }
  dma_channel_reg_t *channel = dma_source->channel;

  kprintf("left_bytes %d src %x dst %x desc %x\n", channel->left_bytes,
          channel->cur_dst_addr, channel->cur_dst_addr, channel->desc_addr);

  channel_count = dma_source->channel_count;

  return (dma_reg->status >> channel_count) & 0x01;
}

int dma_test() {
  u32 len = 512 * 1024;
  len = ALIGN(len, 4);

  u32 *src_addr = (u32 *)0x40100000;  // kmalloc(len, DEVICE_TYPE);  //
  u32 *dst_addr = (u32 *)0x40200000;  // kmalloc(len, DEVICE_TYPE);  //

  page_map(src_addr, src_addr, 0);
  page_map(dst_addr, dst_addr, 0);

  dma_set_t dma_set;
  u32 hdma, st = 0;
  u32 timeout;
  u32 i, valid;

  kprintf("DMA: test 0x%08x ====> 0x%08x, len %uKB \r\n", (u32)src_addr,
          (u32)dst_addr, (len / 1024));

  // dma
  dma_set.loop_mode = 0;
  dma_set.wait_cyc = 8;
  dma_set.data_block_size = 1 * 32 / 8;
  // channel config (from dram to dram)
  dma_set.channel_cfg.src_drq_type = DMAC_CFG_TYPE_DRAM;  // dram
  dma_set.channel_cfg.src_addr_mode = DMAC_CFG_DEST_ADDR_TYPE_LINEAR_MODE;
  dma_set.channel_cfg.src_burst_length = DMAC_CFG_SRC_4_BURST;
  dma_set.channel_cfg.src_data_width = DMAC_CFG_SRC_DATA_WIDTH_32BIT;
  dma_set.channel_cfg.reserved0 = 0;

  dma_set.channel_cfg.dst_drq_type = DMAC_CFG_TYPE_DRAM;  // dram
  dma_set.channel_cfg.dst_addr_mode = DMAC_CFG_DEST_ADDR_TYPE_LINEAR_MODE;
  dma_set.channel_cfg.dst_burst_length = DMAC_CFG_DEST_4_BURST;
  dma_set.channel_cfg.dst_data_width = DMAC_CFG_SRC_DATA_WIDTH_32BIT;
  dma_set.channel_cfg.reserved1 = 0;

  hdma = dma_request(0);
  if (!hdma) {
    kprintf("DMA: can't request dma\r\n");
    return -1;
  }

  dma_setting(hdma, &dma_set);

  // prepare data
  for (i = 0; i < (len / 4); i += 4) {
    src_addr[i] = i;
    src_addr[i + 1] = i + 1;
    src_addr[i + 2] = i + 2;
    src_addr[i + 3] = i + 3;
    // kprintf("src %d %x\n", i, src_addr[i]);
  }

  // timeout : 100 ms
  timeout = cpu_read_ms();

  dma_start(hdma, (u32)src_addr, (u32)dst_addr, len);
  st = dma_querystatus(hdma);

  while ((cpu_read_ms() - timeout < 100) && st) {
    st = dma_querystatus(hdma);
    kprintf("read ms %d\n", cpu_read_ms());
  }

  if (st) {
    kprintf("DMA: test timeout! ret=%x\r\n", st);
    dma_stop(hdma);
    dma_release(hdma);

    return -2;
  } else {
    valid = 1;
    // Check data is valid
    for (i = 0; i < (len / 4); i += 4) {
      // kprintf("dst %d %x\n", i, dst_addr[i]);

      if (dst_addr[i] != i || dst_addr[i + 1] != i + 1 ||
          dst_addr[i + 2] != i + 2 || dst_addr[i + 3] != i + 3) {
        valid = 0;
        break;
      }
    }
    if (valid) {
      kprintf("DMA: test OK in %lums\r\n", (cpu_read_ms() - timeout));
    } else {
      kprintf("DMA: test check failed at %u bytes\r\n", i);
    }
  }

  dma_stop(hdma);
  dma_release(hdma);

  return 0;
}

u32 dma_init(u32 channel) { dma_init_all(); }

u32 dma_trans(u32 channel, u32 mode, void *addr, void *phyaddr, size_t size) {
  if (dma_init_ok <= 0) {
    dma_init_all();
  }

  dma_set_t dma_set;

  return 1;
}