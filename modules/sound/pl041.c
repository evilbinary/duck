/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "dev/devfs.h"
#include "kernel/irq.h"
#include "kernel/kernel.h"
#include "kernel/page.h"
#include "platform/versatilepb/gpio.h"
#include "sound.h"

#define AACI_BASE 0x10004000

#define AACI_RXCR1 0x00
#define AACI_TXCR1 0x04
#define AACI_SR1 0x08
#define AACI_ISR1 0x0C
#define AACI_IE1 0x10
#define AACI_SL1TX 0x54
#define AACI_SL2TX 0x5C
#define AACI_SLFR 0x68
#define AACI_INTCLR 0x74
#define AACI_MAINCR 0x78
#define AACI_RESET 0x7C
#define AACI_DR1_0 0x90

#define AACI_MAINCR_AACIFE (1 << 0)
#define AACI_MAINCR_SL1TXEN (1 << 4)
#define AACI_MAINCR_SL12TXEN (1 << 8)

#define AACI_TXCR_TXEN (1 << 0)
#define AACI_TXCR_SLOTS (0x1E) /* slot1..slot4 */
#define AACI_TXCR_COMPACT (1 << 15)

#define AACI_SR_TXFE (1 << 1)
#define AACI_SR_TXHE (1 << 3)
#define AACI_SR_TXFF (1 << 5)

#define AACI_IE_TXIE (1 << 2)
#define AACI_INTCLR_TXUEC1 (1 << 5)

/* 有中断框架时走环形缓冲 + TX 中断，否则退化为轮询直写。 */
#if defined(VERSATILE_USE_IRQ_CHIP) && VERSATILE_USE_IRQ_CHIP
#define AACI_USE_IRQ 1
#else
#define AACI_USE_IRQ 0
#endif

static int g_rate = 48000;

static inline void aaci_write(u32 off, u32 val) {
  *(volatile u32 *)(AACI_BASE + off) = val;
}

static inline u32 aaci_read(u32 off) {
  return *(volatile u32 *)(AACI_BASE + off);
}

/* LM4549 codec：先写数据槽 sl2tx，再写控制槽 sl1tx（QEMU 在 sl1tx 写入时
 * 读取 sl2tx 的值）。寄存器地址 7bit，数据 16bit。 */
static void aaci_codec_write(u32 reg, u32 val) {
  aaci_write(AACI_SL2TX, (val & 0xFFFF) << 4);
  aaci_write(AACI_SL1TX, (reg & 0x7F) << 12);
}

#if AACI_USE_IRQ
/* 环形缓冲：写端在应用线程，读端在 AACI TX 中断。单核下用无锁环形，
 * 先写数据后更新写指针，中断看到旧写指针就不会读到半成品。 */
#define AACI_RING_WORDS 16384
#define AACI_RING_MASK (AACI_RING_WORDS - 1)
static u32 g_ring[AACI_RING_WORDS];
static volatile u32 g_r, g_w;

static u32 ring_count(void) { return (g_w - g_r) & AACI_RING_MASK; }

static int ring_push(u32 v) {
  u32 n = (g_w + 1) & AACI_RING_MASK;
  if (n == g_r) {
    return 0;
  }
  g_ring[g_w] = v;
  g_w = n;
  return 1;
}

static int ring_pop(u32 *v) {
  if (g_r == g_w) {
    return 0;
  }
  *v = g_ring[g_r];
  g_r = (g_r + 1) & AACI_RING_MASK;
  return 1;
}

/* 把环形缓冲里的样本尽可能填进 FIFO */
static void aaci_fill(void) {
  while (!(aaci_read(AACI_SR1) & AACI_SR_TXFF)) {
    u32 v;
    if (!ring_pop(&v)) {
      break;
    }
    aaci_write(AACI_DR1_0, v);
  }
}

static int aaci_irq_handler(u32 irq, void *arg) {
  /* 清 underrun（TXHE 只能靠填数据清，不能写清） */
  aaci_write(AACI_INTCLR, AACI_INTCLR_TXUEC1);
  aaci_fill();
  /* 环里没数据了就关 TX 中断：FIFO 空时 TXHE 恒为 1，若继续使能会中断风暴 */
  if (ring_count() == 0) {
    aaci_write(AACI_IE1, 0);
  }
  return 1;
}
#endif

static size_t read(device_t *dev, void *buf, size_t len) { return 0; }

static size_t write(device_t *dev, void *buf, size_t len) {
  u32 *p = (u32 *)buf;
  size_t words = len / 4;
  size_t i;

#if AACI_USE_IRQ
  for (i = 0; i < words; i++) {
    u32 guard = 0;
    while (!ring_push(p[i])) {
      /* YiYiYa: 原上限 2000 万是天文数字——中断/硬件不排水时会烧掉整颗核
         （实测 fceux 的 count 飙到 1147 万、主循环被拖住）。这里收成 2000：
         最多丢一点样本，绝不拖死调用方。 */
      if (++guard > 2000u) {
        return i * 4;
      }
    }
  }
  /* 有数据了：重新使能 TX 半空中断，并把 FIFO 先垫满 */
  aaci_write(AACI_IE1, AACI_IE_TXIE);
  aaci_fill();
  return len;
#else
  for (i = 0; i < words; i++) {
    u32 guard = 0;
    while (aaci_read(AACI_SR1) & AACI_SR_TXFF) {
      /* YiYiYa: 同上，非中断路径也是 2000 万上限，硬件不排水时同样烧核 */
      if (++guard > 2000u) {
        return i * 4;
      }
    }
    aaci_write(AACI_DR1_0, p[i]);
  }
  return len;
#endif
}

void codec_init() {}

size_t sound_ioctl(device_t *dev, u32 cmd, void *args) {
  u32 ret = 0;
  if (args == NULL) {
    return 0;
  }
  if (cmd == SNDCTL_DSP_GETFMTS) {
    u32 *val = args;
    *val = AFMT_S16_LE;
  } else if (cmd == SNDCTL_DSP_SETFMT) {
    u32 *val = args;
    if (*val != AFMT_S16_LE) {
      ret = -1;
    }
  } else if (cmd == SNDCTL_DSP_CHANNELS) {
    u32 *val = args;
    *val = 2;
  } else if (cmd == SNDCTL_DSP_SPEED) {
    u32 *val = args;
    if (*val >= 8000 && *val <= 48000) {
      g_rate = (int)*val;
      aaci_codec_write(0x2C, (u32)g_rate); /* PCM front DAC rate */
    }
    *val = (u32)g_rate;
  } else if (cmd == SNDCTL_DSP_SETFRAGMENT) {
    /* 使用固定 FIFO，忽略分片请求 */
  } else if (cmd == IOC_STAT || cmd == IOC_STATFS) {
    struct stat *stat = args;
    stat->st_mode = S_IFCHR;
  }

  return ret;
}

int sound_init(void) {
  log_info("sound init\n");

  page_map(AACI_BASE, AACI_BASE, PAGE_DEV);

  /* 写 maincr 且 AACIFE=0 会让 QEMU 复位 AACI（清 FIFO/寄存器） */
  aaci_write(AACI_MAINCR, 0);
  aaci_write(AACI_MAINCR,
             AACI_MAINCR_AACIFE | AACI_MAINCR_SL1TXEN | AACI_MAINCR_SL12TXEN);

  /* compact 模式：一次写 32bit = 左右两个 16bit 样本 */
  aaci_write(AACI_TXCR1, AACI_TXCR_TXEN | AACI_TXCR_SLOTS | AACI_TXCR_COMPACT);

  aaci_codec_write(0x2C, (u32)g_rate);

#if AACI_USE_IRQ
  g_r = g_w = 0;
  aaci_write(AACI_IE1, 0); /* 首次 write 再打开 TX 中断 */
  irq_register(IRQ_SIC_AACI, aaci_irq_handler, NULL, "aaci", 0);
  irq_enable(IRQ_SIC_AACI);
#endif

  device_t *dev = kmalloc(sizeof(device_t), DEFAULT_TYPE);
  dev->name = "sound";
  dev->read = read;
  dev->write = write;
  dev->ioctl = sound_ioctl;
  dev->id = DEVICE_SB;
  dev->type = DEVICE_TYPE_BLOCK;
  device_add(dev);

  vnode_t *dsp = vfs_create_node("dsp", V_FILE | V_BLOCKDEVICE);
  dsp->device = device_find(DEVICE_SB);
  dsp->op = &device_operator;
  vfs_mount(NULL, "/dev", dsp);

  vnode_t *dsp0 = vfs_create_node("dsp0", V_FILE | V_BLOCKDEVICE);
  dsp0->device = device_find(DEVICE_SB);
  dsp0->op = &device_operator;
  vfs_mount(NULL, "/dev", dsp0);

  codec_init();

  return 0;
}

void sound_exit(void) { kprintf("sound exit\n"); }

module_t sound_module = {
    .name = "sound", .init = sound_init, .exit = sound_exit};
