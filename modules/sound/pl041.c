/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "dev/devfs.h"
#include "kernel/kernel.h"
#include "kernel/page.h"
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
#define AACI_MAINCR_DMAENABLE (1 << 9)

#define AACI_TXCR_TXEN (1 << 0)
#define AACI_TXCR_SLOTS (0x1E) /* slot1..slot4 */
#define AACI_TXCR_COMPACT (1 << 15)

#define AACI_SR_TXFE (1 << 1)
#define AACI_SR_TXHE (1 << 3)
#define AACI_SR_TXFF (1 << 5)

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

static size_t read(device_t *dev, void *buf, size_t len) { return 0; }

static size_t write(device_t *dev, void *buf, size_t len) {
  u32 *p = (u32 *)buf;
  size_t words = len / 4;
  size_t i;

  for (i = 0; i < words; i++) {
    u32 guard = 0;
    while (aaci_read(AACI_SR1) & AACI_SR_TXFF) {
      if (++guard > 20000000u) {
        return i * 4;
      }
    }
    aaci_write(AACI_DR1_0, p[i]);
  }
  return len;
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
