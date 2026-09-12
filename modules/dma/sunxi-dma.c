/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "sunxi-dma.h"

#include "dma.h"
#include "kernel/kernel.h"

#define SUNXI_DMA_MAX 16
// #define kprintf
#define log_debug

static int dma_init_ok = -1;
static dma_source_t dma_channel_source[SUNXI_DMA_MAX];

// static dma_desc_t dma_channel_desc[SUNXI_DMA_MAX]
// __attribute__((aligned(64)));

dma_desc_t *dma_channel_desc = NULL;

/* DMA 描述符必须落在 DRAM 物理区间（内核 identity 映射区）内才允许对其做
 * 按 MVA 的 cache 维护：地址非法时直接跳过，避免在中断里触发数据异常。 */
static inline int is_dma_desc_addr(const void* p) {
  u32 a = (u32)(unsigned long)p;
  return (p != NULL) && (a >= 0x40000000u) && (a < 0x80000000u);
}

/* 【只对内存做 cache 维护】按 MVA 的 cache 维护一旦落在**未映射/设备地址**上就会
 * 触发数据异常；DMA 路径常在中断里执行，异常会来不及 EOI，把低优先级中断（定时器）
 * 永久屏蔽 —— 实测现象：播放音频后 app_fps=0、日志时间戳冻结。
 * 典型踩坑：音频 DMA 的目的地址是 CODEC 寄存器 `CODEC_BASE+0x20`（设备地址！），
 * 对它按 32B 步进失效，走出已映射的 codec 页后第一个地址 0x02031000 就炸。
 * 约定：0x0000_0000~0x3FFF_FFFF 为 MMIO/设备区（GIC/CCU/CODEC/DMA/定时器…），
 * 一律不做 cache 维护；DMA 缓冲只可能是 DRAM identity(0x4000_0000~) 或用户 VA。 */
static inline int dma_buf_cacheable(const void* p, size_t len) {
  u32 a = (u32)(unsigned long)p;
  if (p == NULL || len == 0) {
    return 0;
  }
  if (a < 0x40000000u) {
    return 0;
  }
  return 1;
}

void *dma_handler(interrupt_context_t *ic) {
  int irq = gic_irqwho();

  int i;
  uint pending;
  u32 channel_no = 0;
  dma_reg_t *dma_reg = (dma_reg_t *)SUNXI_DMA_BASE;

  for (i = 0; i < 8 && i < SUNXI_DMA_MAX; i++) {
    pending = (DMA_PKG_END_INT << (i * 4));
    if (dma_reg->irq_pending0 & pending) {
      dma_reg->irq_pending0 = pending;
      log_debug("dma pedding %d\n", i);

      /* 【cache 一致性】描述符是 CPU 与 DMA 共享的内存：DMA 硬件写它时绕过 CPU
       * cache，回调里若读 desc 状态（或重新投喂）必须先失效，否则读到旧值。
       * 【必须校验地址】按 MVA 的 cache 维护对"未映射地址"会触发数据异常；本函数
       * 在中断里执行，一旦异常就来不及 EOI，会把更高/同级中断堵死（实测：音频
       * 播放时定时器被永久屏蔽 ⇒ app_fps=0、时间戳冻结）。DMA 描述符必定在 DRAM
       * 物理区间内，故只在该区间内才做维护。 */
      if (is_dma_desc_addr(dma_channel_source[i].desc)) {
        cpu_invalidate_dcache_range((unsigned long)dma_channel_source[i].desc,
                        (unsigned long)dma_channel_source[i].desc +
                            sizeof(dma_desc_t));
      }
      if (dma_channel_source[i].dma_func.m_func != NULL) {
        dma_channel_source[i].dma_func.m_func(
            dma_channel_source[i].dma_func.m_data);
      }
    }
  }
  for (i = 8; i < SUNXI_DMA_MAX; i++) {
    pending = (DMA_PKG_END_INT << ((i - 8) * 4));
    if (dma_reg->irq_pending1 & pending) {
      dma_reg->irq_pending1 = pending;
      /* 校验见上：非法/未映射地址不得做 cache 维护 */
      if (is_dma_desc_addr(dma_channel_source[i].desc)) {
        cpu_invalidate_dcache_range((unsigned long)dma_channel_source[i].desc,
                        (unsigned long)dma_channel_source[i].desc +
                            sizeof(dma_desc_t));
      }
      if (dma_channel_source[i].dma_func.m_func != NULL) {
        dma_channel_source[i].dma_func.m_func(
            dma_channel_source[i].dma_func.m_data);
      }
    }
  }

  gic_irqack(irq);
  // kprintf("dma handler %d\n", irq);

  return NULL;
}

void dma_init_all(void) {
  int i;
  dma_reg_t *const dma_reg = (dma_reg_t *)DMA_BASE;
  if (dma_init_ok > 0) return;

  log_debug("dma init\n");
  page_map(DMA_BASE, DMA_BASE, PAGE_DEV);

#ifdef T113_S3
  log_debug("ccu base %x\n", ccu);
  volatile ccu_reg_t *const ccu = (ccu_reg_t *)CCU_BASE;

  /* dma : mbus clock gating */
  ccu->mbus_gate |= 1 << 0;  // DMA_MCLK_EN

  log_debug("ccu bus gate %x\n", ccu->mbus_gate);

  /* dma reset */
  ccu->dma_gate_reset |= 1 << DMA_RST_OFS;

  /* dma gating */
  ccu->dma_gate_reset |= 1 << DMA_GATING_OFS;

  log_debug("ccu gate reset %x\n", ccu->dma_gate_reset);

#else defined(V3S)

  // dma bus gate, deassert
  u32 reg = io_read32(V3S_CCU_BASE + CCU_BUS_SOFT_RST0);
  io_write32(V3S_CCU_BASE + CCU_BUS_SOFT_RST0, reg | 1 << 6);

  // dma gating
  reg = io_read32(CCU_BASE + CCU_BUS_CLK_GATE0);
  io_write32(V3S_CCU_BASE + CCU_BUS_CLK_GATE0, reg | 1 << 6);

#endif

  dma_reg->irq_en0 = 0;
  dma_reg->irq_en1 = 0;

  dma_reg->irq_pending0 = 0xffffffff;
  dma_reg->irq_pending1 = 0xffffffff;

  /* auto MCLK gating  disable */
  dma_reg->auto_gate &= ~(0x7 << 0);
  dma_reg->auto_gate |= 0x7 << 0;

  log_debug("dma_reg auto_gate %x\n", dma_reg->auto_gate);

  memset((void *)dma_channel_source, 0, SUNXI_DMA_MAX * sizeof(dma_source_t));

  dma_channel_desc =
      kmalloc_alignment(sizeof(dma_desc_t) * SUNXI_DMA_MAX, 64, DEVICE_TYPE);

  for (i = 0; i < SUNXI_DMA_MAX; i++) {
    dma_channel_source[i].used = 0;
    dma_channel_source[i].channel = &(dma_reg->channel[i]);
    dma_channel_source[i].desc = &dma_channel_desc[i];

    log_debug("dma %d channel %x desc %x channel addr %x\n", i,
              dma_channel_source[i].channel, dma_channel_source[i].desc,
              &dma_channel_source[i]);
  }

  dma_init_ok = 1;

  exception_regist(EX_DMA, dma_handler);
  log_debug("dma init end\n");
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

u32 dma_request(u32 channel) {
  int i = channel;
  if (channel < SUNXI_DMA_MAX) {
    dma_channel_source[i].used = 1;
    dma_channel_source[i].channel_count = channel;
    return (u32)&dma_channel_source[i];
  }
  return 0;
}

int sunxi_dma_release(u32 hdma) {
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

void dma_set_mode(u32 hdma, u32 mode, dma_interrupt_handler_t fun, void *data) {
  dma_reg_t *dma_reg = (dma_reg_t *)DMA_BASE;
  dma_source_t *dma_source = (dma_source_t *)hdma;
  u32 channel_no = dma_source->channel_count;

  dma_set_t dma_set;

  // dma
  dma_set.loop_mode = 1;
  dma_set.wait_cyc = 0;
  dma_set.data_block_size = 16 / 8;
  // channel config (from dram to audio io)
  dma_set.channel_cfg.src_drq_type = DMAC_CFG_TYPE_DRAM;  // dram
  dma_set.channel_cfg.src_addr_mode = DMAC_CFG_SRC_ADDR_TYPE_LINEAR_MODE;
  dma_set.channel_cfg.src_burst_length = DMAC_CFG_SRC_16_BURST;
  dma_set.channel_cfg.src_data_width = DMAC_CFG_SRC_DATA_WIDTH_32BIT;
  dma_set.channel_cfg.reserved0 = 0;

  dma_set.channel_cfg.dst_drq_type = DMAC_CFG_TYPE_AUDIO;
  dma_set.channel_cfg.dst_addr_mode = DMAC_CFG_DEST_ADDR_TYPE_IO_MODE;
  dma_set.channel_cfg.dst_burst_length = DMAC_CFG_DEST_16_BURST;
  dma_set.channel_cfg.dst_data_width = DMAC_CFG_DEST_DATA_WIDTH_16BIT;
  dma_set.channel_cfg.reserved1 = 0;

  dma_set.channel_cfg.src_burst_length = DMAC_CFG_SRC_4_BURST;
  dma_set.channel_cfg.src_data_width = DMAC_CFG_SRC_DATA_WIDTH_32BIT;
  dma_set.channel_cfg.dst_burst_length = DMAC_CFG_DEST_4_BURST;
  dma_set.channel_cfg.dst_data_width = DMAC_CFG_DEST_DATA_WIDTH_32BIT;

  if (mode >= 1) {
    u16 type = mode >> 18;
    u8 src_drq_type = mode & 0xff;
    u8 dst_drq_type = mode >> 8;
    if (src_drq_type > 0) {
      kprintf("src_drq_type %d\n", src_drq_type);

      dma_set.channel_cfg.src_drq_type = src_drq_type;
    }
    if (dst_drq_type > 0) {
      kprintf("dst_drq_type %d\n", dst_drq_type);

      dma_set.channel_cfg.dst_drq_type = dst_drq_type;
    }
    u16 addr_mode = mode & 0xffff;
    if (addr_mode >> 8 > 0) {
      dma_set.channel_cfg.src_addr_mode = addr_mode >> 8;
    }
    if (addr_mode & 0xff > 0) {
      dma_set.channel_cfg.dst_addr_mode = addr_mode & 0xff;
    }

    if (channel_no < 8) {
      dma_reg->irq_en0 |= ((DMA_PKG_END_INT) << (channel_no * 4));
      log_debug("dma reg %x channel %x\n", dma_reg->irq_en0, channel_no);
    } else {
      dma_reg->irq_en1 |= ((DMA_PKG_END_INT) << ((channel_no - 8) * 4));
    }
    dma_source->dma_func.m_data = data;
    dma_source->dma_func.m_func = fun;

    gic_irq_priority(0, IRQ_DMAC, 10);
    gic_irq_enable(IRQ_DMAC);
  }

  log_debug("dma init settting\n");
  dma_setting(hdma, &dma_set);
}

int dma_start(u32 hdma, u32 saddr, u32 daddr, u32 bytes) {
  dma_source_t *dma_source = (dma_source_t *)hdma;
  dma_channel_reg_t *channel = dma_source->channel;
  dma_desc_t *desc = dma_source->desc;

  u32 channel_no = dma_source->channel_count;

  if (!dma_source->used) {
    log_debug("dma is used\n");
    return -1;
  }

  log_debug("dma desc %x channel %x\n", desc, channel);

  /*config desc */
  desc->source_addr = saddr;
  desc->dest_addr = daddr;
  desc->byte_count = bytes;

  if (is_dma_desc_addr(desc)) {
    cpu_flush_dcache_range(desc, (u32)desc + sizeof(dma_desc_t));
  }
  /* start dma */
  dmb();
  channel->desc_addr = (u32)desc;
  dmb();
  channel->enable = 1;
  channel->pause = 0;
  dmb();
  log_debug("dma src %x des %x count %d\n", desc->source_addr, desc->dest_addr,
            desc->byte_count);
  return 0;
}

int sunxi_dma_stop(u32 hdma) {
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

  // kprintf("left_bytes %d src %x dst %x desc %x\n", channel->left_bytes,
  //         channel->cur_src_addr, channel->cur_dst_addr, channel->desc_addr);

  channel_count = dma_source->channel_count;

  return (dma_reg->status >> channel_count) & 0x01;
}

int dma_test() {
  u32 len = 512 * 1024;
  len = ALIGN(len, 4);

  u32 *src_addr = (u32 *)0x40100000;  // kmalloc(len, DEVICE_TYPE);  //
  u32 *dst_addr = (u32 *)0x40200000;  // kmalloc(len, DEVICE_TYPE);  //

  /* DMA 测试缓冲：CPU 与 DMA 硬件共享 ⇒ CPU 与硬件共享缓冲（非缓存）。 */
  page_map(src_addr, src_addr, PAGE_SHARED);
  page_map(dst_addr, dst_addr, PAGE_SHARED);

  dma_set_t dma_set;
  u32 hdma, st = 0;
  u32 timeout;
  u32 i, valid;

  log_debug("DMA: test 0x%08x ====> 0x%08x, len %uKB \r\n", (u32)src_addr,
            (u32)dst_addr, (len / 1024));

  // dma
  dma_set.loop_mode = 1;
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
    log_debug("DMA: can't request dma\r\n");
    return -1;
  }

  dma_set_mode(hdma, 1, NULL, NULL);

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
    sunxi_dma_stop(hdma);
    sunxi_dma_release(hdma);

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

  sunxi_dma_stop(hdma);
  sunxi_dma_release(hdma);

  return 0;
}

void dma_stop(u32 channel) {
  u32 hdma = dma_request(channel);
  sunxi_dma_stop(hdma);
  sunxi_dma_release(hdma);
}

u32 dma_init(u32 channel, u32 mode, dma_interrupt_handler_t handler,
             void *data) {
  dma_init_all();

  dma_set_t dma_set;
  u32 hdma, st = 0;
  u32 timeout = 0;

  log_debug("dma init request\n");

  hdma = dma_request(channel);

  log_debug("dma init request %x\n", hdma);

  log_debug("dma init set mode\n");
  dma_set_mode(hdma, mode, handler, data);

  log_debug("dma init end\n");
}

u32 dma_trans(u32 channel, void *src, void *dst, size_t len) {
  if (dma_init_ok <= 0) {
    dma_init_all();
  }

  u32 hdma, st = 0;
  u32 timeout = 0;

  hdma = dma_request(channel);
  if (!hdma) {
    log_debug("DMA: can't request dma\r\n");
    return -1;
  }
  log_debug("dma trans start ==>%x to %x len %d\n", src, dst, len);

  dma_source_t *dma_source = (dma_source_t *)hdma;

  /* 源缓冲是内存才刷；若它是设备地址（某些驱动会拿寄存器当"源"）则跳过，
   * 避免按 MVA 的维护落在设备区触发数据异常。 */
  if (dma_buf_cacheable(src, len)) {
    cpu_flush_dcache_range(src, (u32)src + len);
  }

  dma_start(hdma, (u32)src, (u32)dst, len);

  timeout = cpu_read_ms();
  while ((cpu_read_ms() - timeout < 100) && st) {
    st = dma_querystatus(hdma);
    log_debug("read ms %d\n", cpu_read_ms());
  }
  log_debug("st ==>%x\n", st);
  if (st) {
    log_debug("DMA: tran timeout! ret=%x\r\n", st);
    // sunxi_dma_stop(hdma);
    sunxi_dma_release(hdma);
    return -1;
  }
  // sunxi_dma_stop(hdma);
  sunxi_dma_release(hdma);

  /* 【cache 一致性】DMA 写目标缓冲同样绕过 CPU cache ⇒ 完成后必须失效，
   * 否则 CPU 读到的是 cache 里的旧内容（src 侧已在启动前 clean）。
   * 【只在目标是内存时才做】音频/寄存器类的目的地址是设备区（如 CODEC_BASE+0x20），
   * 对设备地址做按 MVA 的失效会数据异常（详见 dma_buf_cacheable 注释）。 */
  if (dma_buf_cacheable(dst, len)) {
    cpu_invalidate_dcache_range((unsigned long)dst, (unsigned long)dst + (unsigned long)len);
  }

  return 1;
}