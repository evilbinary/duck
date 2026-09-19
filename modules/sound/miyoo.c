/*******************************************************************
 * miyoo (SSD202D) 音频驱动 —— BACH 块，KISS 版
 *
 * 硬件模型（照 ewokos machines/miyoo/system/drivers/audio 的寄存器序列 ✓）：
 *   BACH 自带 DMA：把一块物理连续 ring 的【物理地址】告诉它，它自己取数送 DAC，
 *   不需要 SoC DMA 控制器（miyoo 的 dma 模块是 dummy ✗ 也不需要它 ✓）。
 *
 * 寄存器都是【16 位宽、字节寻址】（volatile u16*）✓。MIU 单位 = 8 字节 ✓
 * （TO_MIUSIZE(x) = x >> 3 ✓，FROM_MIUSIZE(level) = level << 3 ✓）。
 *
 * KISS 约定：
 *   · 只支持 S16LE 双声道，采样率 44100/48000 二选一（默认 44100 ✓）
 *   · 64KB 静态 ring ✓；write() 写 ring、轮询水位等待，不用中断 ✓
 *   · 数据写进 ring 后必须刷 dcache ✓（DMA 直读内存 ✗ 会读到旧 cache 行 ✓）
 ********************************************************************/
#include "dev/devfs.h"
#include "dma/dma.h"
#include "kernel/kernel.h"
#include "sound.h" /* SNDCTL_DSP_*/AFMT_* 等 OSS ioctl 常量（照 t113-s3/dummy ✓） */

/* ---- 寄存器基址（MIU 偏移，物理 = MIU + 0x20000000）----
 * ✗ 踩过：直接用裸偏移访问 0x28400 ⇒ 未映射 ⇒ 内核态 memory fault。
 * miyoo 内核恒等映射，物理地址可直接访问，但必须带上 0x20000000 基址 ✓ */
#define AUD_MIU_BASE 0x20000000U
#define R_BACH_TOP (AUD_MIU_BASE + 0x00206800U) /* audiotop：电源/垫脚/编解码模拟部分 */
#define R_BACH_CLK (AUD_MIU_BASE + 0x0028400U)  /* 音频时钟开关 */
#define R_BACH (AUD_MIU_BASE + 0x002a0400U)     /* BACH 数字部分 */

/* ---- 16 位寄存器访问 ---- */
static inline void w16(u32 off, u16 v) {
  *(volatile u16 *)(uintptr_t)(R_BACH + off) = v;
}
static inline u16 r16(u32 off) {
  return *(volatile u16 *)(uintptr_t)(R_BACH + off);
}
static inline void wtop(u32 off, u16 v) {
  *(volatile u16 *)(uintptr_t)(R_BACH_TOP + off) = v;
}

/* ---- BACH 数字部分偏移 ---- */
#define BACH_SR0_SEL 0x04      /* bits4-7：采样率选择（src rates 表下标） */
#define BACH_TEST_CTRL7 0x1dc  /* bit15=rd_mono bit13=rd_mono_copy */
#define CH0 0x100              /* DMA 通道 0（chan_offset = 0x100 + 0x40*i） */
#define CH_CTRL0 0x00          /* bit0=rst bit1=en bit2=live_count_en */
#define SUB 0x04               /* 子通道 0 基址（= chan + 4 + 0x20*j） */
#define SUB_EN 0x00            /* bit15=en bit14=init bit13=trigger bit12=count
                                  bits0-11=addr_lo */
#define SUB_ADDR_HI 0x02       /* bits0-14：ring 物理地址高段（miu>>12） */
#define SUB_BUFFSZ 0x04        /* ring 总大小（MIU 单位） */
#define SUB_SIZE 0x06          /* 目标水位（MIU 单位）：写数据后设它并打 trigger */
#define SUB_UNDERRUN_TH 0x0a   /* 欠载阈值（MIU 单位） */
#define SUB_LEVEL 0x0e         /* 当前水位（先置 count=1 锁存再读 ✓） */

#define TO_MIUSIZE(x) ((x) >> 3)
#define FROM_MIUSIZE(x) ((x) << 3)

/* 采样率表（ewokos msc313_bach_src_rates[] 的下标 ✓） */
static const int src_rates[] = {8000,  11025, 12000, 16000, 22050,
                                24000, 32000, 44100, 48000};

#define RING_SIZE (64 * 1024)
static u8 snd_ring[RING_SIZE] __attribute__((aligned(64)));
static u32 snd_wr;    /* 应用写到 ring 的字节偏移 */
static u32 snd_level; /* 已 queue 进硬件、尚未播完的字节数 */
static int snd_rate = 44100;
static int snd_started = 0;

/* audiotop 固定初始化表（ewokos miyoo-dais.c probe 原样 ✓） */
static const struct {
  u32 off;
  u16 val;
} audiotop_init[] = {
    {0x00, 0x0A14}, {0x04, 0x0030}, {0x08, 0x0080}, {0x0C, 0x01A5},
    {0x20, 0x3000}, {0x84, 0x3C1E},
};

static inline u32 miu_of(u32 addr) { return addr - AUD_MIU_BASE; }

/* 读当前水位（字节）✓：count=1 锁存 → 连读两次一致才信（照 ewokos ✓） */
static u32 bach_level(void) {
  u16 last, level;
  int tries;
  w16(CH0 + SUB + SUB_EN, r16(CH0 + SUB + SUB_EN) | (1 << 12));
  for (tries = 0, last = 0xffff, level = 0; tries < 8; tries++) {
    level = r16(CH0 + SUB + SUB_LEVEL);
    if (level == last) {
      break;
    }
    last = level;
  }
  w16(CH0 + SUB + SUB_EN, r16(CH0 + SUB + SUB_EN) & ~(1 << 12));
  return FROM_MIUSIZE(level);
}

/* 往 ring 里 queue n 字节（水位目标 = level + n，打 trigger ✓） */
static void bach_queue(u32 n) {
  u32 target = TO_MIUSIZE(snd_level + n);
  w16(CH0 + SUB + SUB_SIZE, (u16)target);
  w16(CH0 + SUB + SUB_EN, r16(CH0 + SUB + SUB_EN) | (1 << 13)); /* trigger=1 */
  w16(CH0 + SUB + SUB_EN, r16(CH0 + SUB + SUB_EN) & ~(1 << 13));
  snd_level += n;
}

static void bach_start(void) {
  u32 miu;
  int i;
  /* 时钟 */
  *(volatile u16 *)(uintptr_t)(R_BACH_CLK + 0x0) = 0x00c0;
  *(volatile u8 *)(uintptr_t)(R_BACH_CLK + 0x1c) = 0x01;
  /* audiotop 固定表：0x00..0xBC【整表写入】（含 0 ✓，避免依赖复位态 ✗）
   * 非零项照 ewokos probe：0x00=0x0A14 0x04=0x30 0x08=0x80 0x0C=0x1A5
   *                       0x20=0x3000 0x84=0x3C1E，其余 0 ✓ */
  for (i = 0; i < 48; i++) {
    u32 off = (u32)i * 4;
    u16 v = 0;
    if (off == 0x00) v = 0x0A14;
    if (off == 0x04) v = 0x0030;
    if (off == 0x08) v = 0x0080;
    if (off == 0x0C) v = 0x01A5;
    if (off == 0x20) v = 0x3000;
    if (off == 0x84) v = 0x3C1E;
    wtop(off, v);
  }
  /* 通道 0：复位 → 立数水位 */
  w16(CH0 + CH_CTRL0, 0x0001);
  w16(CH0 + CH_CTRL0, 0x0004); /* live_count_en=1 */
  /* 子通道：init 1→0（复位水位） */
  w16(CH0 + SUB + SUB_EN, 0);
  w16(CH0 + SUB + SUB_EN, (1 << 14));
  w16(CH0 + SUB + SUB_EN, 0);
  /* ring：物理地址（miyoo 恒等映射 ⇒ 虚拟即物理 ✓） */
  miu = miu_of((u32)(uintptr_t)snd_ring);
  w16(CH0 + SUB + SUB_EN, (u16)((miu & 0xfff) | (1 << 14))); /* addr_lo */
  w16(CH0 + SUB + SUB_ADDR_HI, (u16)((miu >> 12) & 0x7fff)); /* addr_hi */
  w16(CH0 + SUB + SUB_BUFFSZ, (u16)TO_MIUSIZE(RING_SIZE));
  w16(CH0 + SUB + SUB_UNDERRUN_TH, (u16)TO_MIUSIZE(1024));
  /* 立体声：清 mono 位 */
  w16(BACH_TEST_CTRL7, (u16)(r16(BACH_TEST_CTRL7) & ~((1 << 15) | (1 << 13))));
  /* 采样率 */
  for (i = 0; i < (int)(sizeof(src_rates) / sizeof(src_rates[0])); i++) {
    if (src_rates[i] == snd_rate) {
      u16 v = r16(BACH_SR0_SEL);
      v = (u16)((v & ~0x00f0) | ((u16)i << 4));
      w16(BACH_SR0_SEL, v);
      break;
    }
  }
  snd_wr = 0;
  snd_level = 0;
  /* 启动：先通道后子通道（顺序照 ewokos ✓） */
  w16(CH0 + CH_CTRL0, r16(CH0 + CH_CTRL0) | 0x0002);
  w16(CH0 + SUB + SUB_EN, r16(CH0 + SUB + SUB_EN) | (1 << 15));
  snd_started = 1;
  log_info("bach start: miu ring=%x rate=%d\n", miu_of((u32)(uintptr_t)snd_ring),
           snd_rate);
  /* 【自检方波】440Hz、S16LE 双声道、约 0.18 秒 ⇒ 上电应听到一声短促"哔"。
   * 有哔 ⇒ 管线通 ✓（问题只会在应用数据 ✗）；没哔 ⇒ 使能位/上电还缺 ✗。 */
  {
    u32 n = RING_SIZE / 2;
    u32 idx;
    for (idx = 0; idx < n / 2; idx++) {
      u16 v = (((idx / 2) / 50) & 1) ? 0x1f00 : (u16)0xe100; /* ±约 7900 */
      snd_ring[idx * 2] = (u8)(v & 0xff);
      snd_ring[idx * 2 + 1] = (u8)(v >> 8);
    }
    cpu_flush_dcache_range((unsigned long)snd_ring,
                           (unsigned long)(snd_ring + n));
    snd_wr = n % RING_SIZE;
    bach_queue(n);
  }
}

/* 【OSS ioctl】照 t113-s3.c 的骨架 ✓：GETFMTS 必须答 AFMT_S16_LE，
 * 否则 SDL 报 "Couldn't find any hardware audio formats" ✗；
 * IOC_STAT/IOC_STATFS 要给 S_IFCHR ✓（应用会 stat 设备 ✗）。
 * SPEED/CHANNELS 收下并尽量套用（KISS：SPEED 映射到 src_rates 表 ✓）。 */
size_t sound_ioctl(device_t *dev, u32 cmd, void *args) {
  u32 ret = 0;
  (void)dev;
  if (cmd == SNDCTL_DSP_GETFMTS) {
    u32 *val = args;
    if (val) {
      *val = AFMT_S16_LE;
    }
  } else if (cmd == SNDCTL_DSP_SETFMT) {
    u32 *val = args;
    ret = val ? *val : 0; /* 只声明支持 S16LE ✓，值原样返回 ✓ */
  } else if (cmd == SNDCTL_DSP_CHANNELS) {
    u32 *val = args;
    (void)val; /* 只支持双声道 ✓，KISS：忽略差异 ✓ */
  } else if (cmd == SNDCTL_DSP_SPEED) {
    u32 *val = args;
    if (val) {
      u32 rate = *val;
      int best = 0;
      int i;
      int diff = 0x7fffffff;
      for (i = 0; i < (int)(sizeof(src_rates) / sizeof(src_rates[0])); i++) {
        int d = (int)rate - src_rates[i];
        if (d < 0) {
          d = -d;
        }
        if (d < diff) {
          diff = d;
          best = i;
        }
      }
      snd_rate = src_rates[best];
      if (snd_started) { /* 已在跑 ⇒ 改分频即可（KISS：不打断播放 ✓） */
        u16 v = r16(BACH_SR0_SEL);
        v = (u16)((v & ~0x00f0) | ((u16)best << 4));
        w16(BACH_SR0_SEL, v);
      }
    }
  } else if (cmd == 8 /* IOC_STAT */ || cmd == 9 /* IOC_STATFS */) {
    /* 结构体第二个字段是 mode（照 t113-s3.c 的用法 ✓） */
    u32 *fields = args;
    if (fields) {
      fields[1] = 0x2000; /* S_IFCHR */
    }
  }
  return ret;
}

static size_t read(device_t *dev, void *buf, size_t len) {
  (void)dev;
  (void)buf;
  (void)len;
  return 0;
}

static size_t write(device_t *dev, const void *buf, size_t len) {
  const u8 *p = (const u8 *)buf;
  size_t left = len;

  if (snd_started == 0) {
    bach_start();
  }
  while (left > 0) {
    u32 level = bach_level();
    u32 free = RING_SIZE - level;
    size_t n;

    if (free < 512) { /* ring 快满 ⇒ 等硬件消费 */
      thread_sleep(thread_current(), 2);
      continue;
    }
    n = left < free ? left : free;
    /* 不跨 ring 末尾的写法：分两段拷（KISS ✓） */
    {
      size_t first = RING_SIZE - snd_wr;
      if (first > n) {
        first = n;
      }
      kmemcpy(snd_ring + snd_wr, p, first);
      if (n > first) {
        kmemcpy(snd_ring, p + first, n - first);
      }
    }
    /* DMA 直读内存 ⇒ 写完必须刷 cache ✓（否则读到旧数据 ✓） */
    {
      u32 tail = snd_wr + (u32)n;
      if (tail > RING_SIZE) {
        cpu_flush_dcache_range((unsigned long)(snd_ring + snd_wr),
                               (unsigned long)(snd_ring + RING_SIZE));
        cpu_flush_dcache_range((unsigned long)snd_ring,
                               (unsigned long)(snd_ring + (tail - RING_SIZE)));
      } else {
        cpu_flush_dcache_range((unsigned long)(snd_ring + snd_wr),
                               (unsigned long)(snd_ring + tail));
      }
    }
    snd_wr = (snd_wr + (u32)n) % RING_SIZE;
    p += n;
    left -= n;
    bach_queue((u32)n);
  }
  return len;
}

void codec_init(void) {
  /* 【设备寄存器页映射】照 t113-s3 的 page_map(CODEC_BASE, CODEC_BASE, PAGE_DEV) ✓。
   * 放在 codec_init（驱动初始化 ✓ 只做一次 ✓），而不是每次播放的 bach_start ✓。
   * 恒等映射只覆盖 RAM，设备寄存器区间不在页表里 ✗
   * （fault 日志 `pte 28000 -> 0` 就是证据 ✓）。按页对齐映射三个区间 ✓。 */
  page_map(R_BACH & ~0xfffU, R_BACH & ~0xfffU, PAGE_DEV); /* BACH 数字 0x202a0400 ✓ */
  page_map(R_BACH_TOP & ~0xfffU, R_BACH_TOP & ~0xfffU, PAGE_DEV); /* audiotop 0x20206800 ✓ */
  page_map(R_BACH_CLK & ~0xfffU, R_BACH_CLK & ~0xfffU, PAGE_DEV); /* 时钟 0x2028400 ✓ */
}

int sound_init(void) {
  device_t *dev = kmalloc(sizeof(device_t), DEFAULT_TYPE);
  vnode_t *dsp;
  vnode_t *dsp0;

  log_info("sound init (BACH KISS)\n");
  dev->name = "sound";
  dev->read = read;
  dev->write = write;
  dev->ioctl = sound_ioctl; /* GETFMTS/STAT 等必须应答 ✓（否则 SDL 报无硬件格式 ✗） */
  dev->id = DEVICE_SB;
  dev->type = DEVICE_TYPE_BLOCK;
  device_add(dev);

  dsp = vfs_create_node("dsp", V_FILE | V_BLOCKDEVICE);
  dsp->device = device_find(DEVICE_SB);
  dsp->op = &device_operator;
  vfs_mount(NULL, "/dev", dsp);

  /* 照 t113-s3.c：再挂一个 dsp0 ✓（有的应用/SDL 变体会找它 ✓） */
  dsp0 = vfs_create_node("dsp0", V_FILE | V_BLOCKDEVICE);
  dsp0->device = device_find(DEVICE_SB);
  dsp0->op = &device_operator;
  vfs_mount(NULL, "/dev", dsp0);

  codec_init();
  return 0;
}

void sound_exit(void) { kprintf("sound exit\n"); }

module_t sound_module = {
    .name = "sound", .init = sound_init, .exit = sound_exit};
