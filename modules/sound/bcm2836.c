/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "dev/devfs.h"
#include "dma/dma.h"
#include "kernel/kernel.h"
#include "sound.h"

static size_t read(device_t* dev, void* buf, size_t len) {
  u32 ret = 0;

  return ret;
}

static size_t write(device_t* dev, void* buf, size_t len) {
  u32 ret = len;

  // sound_play(buf, len);
  return ret;
}

void codec_init() {}

size_t sound_ioctl(device_t* dev, u32 cmd, void* args) {
  u32 ret = 0;
  if (args == NULL) {
    return 0;
  }
  if (cmd == SNDCTL_DSP_GETFMTS) {
    u32* val = args;
    *val = AFMT_S16_LE;
  } else if (cmd == SNDCTL_DSP_CHANNELS) {
    u32* val = args;
    kprintf("SNDCTL_DSP_CHANNELS %d\n", *val);

  } else if (cmd == SNDCTL_DSP_SPEED) {
    u32* val = args;
    kprintf("SNDCTL_DSP_SPEED %d\n", *val);

    kprintf("dma_init end\n");
  } else if (cmd == IOC_STAT) {
    struct stat* stat = args;
    stat->st_mode = S_IFCHR;
  } else if (cmd == IOC_STATFS) {
    kprintf("sound_ioctl2 %d\n", cmd);
    struct stat* stat = args;
    stat->st_mode = S_IFCHR;
  } else {
    kprintf("sound_ioctl %d\n", cmd);
  }

  return ret;
}

int sound_init(void) {
  log_info("sound init\n");
  device_t* dev = kmalloc(sizeof(device_t), DEFAULT_TYPE);
  dev->name = "sound";
  dev->read = read;
  dev->write = write;
  dev->ioctl = sound_ioctl;
  dev->id = DEVICE_SB;
  dev->type = DEVICE_TYPE_BLOCK;
  device_add(dev);

  // dsp
  vnode_t* dsp = vfs_create_node("dsp", V_FILE | V_BLOCKDEVICE);
  dsp->device = device_find(DEVICE_SB);
  dsp->op = &device_operator;
  vfs_mount(NULL, "/dev", dsp);

  codec_init();

  return 0;
}

void sound_exit(void) { kprintf("sound exit\n"); }

module_t sound_module = {
    .name = "sound", .init = sound_init, .exit = sound_exit};
