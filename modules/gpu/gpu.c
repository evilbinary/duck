/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "gpu.h"
#include "dev/devfs.h"
#include "vga/vga.h"

size_t gpu_read(device_t* dev, void* buf, size_t len) {
  u32 ret = 0;
  return ret;
}

size_t gpu_write(device_t* dev, const void* buf, size_t len) {
  u32 ret = 0;
  vga_device_t* vga = dev->data;
  if (vga == NULL) {
    kprintf("not found vga\n");
    return ret;
  }
  kstrncpy(vga->frambuffer, (const char*)buf, len);
  return len;
}

size_t gpu_ioctl(device_t* dev, u32 cmd, void* args) {
  u32 ret = 0;
  vga_device_t* vga = dev->data;
  if (vga == NULL) {
    kprintf("not found vga\n");
    return ret;
  }
  if (cmd == VGA_IOC_READ_FRAMBUFFER) {
    ret = (u32)(uintptr_t)vga->frambuffer;
  } else if (cmd == VGA_IOC_READ_FRAMBUFFER_WIDTH) {
    ret = vga->width;
  } else if (cmd == VGA_IOC_READ_FRAMBUFFER_HEIGHT) {
    ret = vga->height;
  } else if (cmd == VGA_IOC_READ_FRAMBUFFER_BPP) {
    ret = vga->bpp;
  } else if (cmd == VGA_IOC_READ_FRAMBUFFER_FORMAT) {
    /* 显示格式（VGA_FMT_*，涵盖颜色空间 + 每像素排布；0=未申报） */
    ret = vga->format;
  } else if (cmd == VGA_IOC_READ_FRAMBUFFER_TRANSFORM) {
    /* 显示变换（旋转 0/90/180/270 + 水平/垂直镜像，见 VGA_ROT_* / VGA_FLIP_*） */
    ret = vga->rotate;
  } else if (cmd == VGA_IOC_FLUSH_FRAMBUFFER) {
    if (vga->frambuffer != NULL && vga->flip_buffer != NULL) {
      u32 offset = (u32)(uintptr_t)args;
      vga->flip_buffer(vga, offset % vga->framebuffer_count);
    }
  } else if (cmd == VGA_IOC_READ_FRAMBUFFER_INFO) {
    /* 用户态是 framebuffer_info_t，比 vga_device_t 短；只拷前段安全字段 */
    if (args != NULL) {
      u32 info[12];
      info[0] = vga->width;
      info[1] = vga->height;
      info[2] = vga->bpp;
      info[3] = vga->mode;
      info[4] = (u32)(uintptr_t)vga->frambuffer;
      info[5] = vga->framebuffer_count;
      info[6] = vga->framebuffer_index;
      info[7] = vga->framebuffer_length;
      info[8] = vga->inited;
      info[9] = 0;
      info[10] = 0;
      info[11] = vga->format;
      kmemcpy(args, info, sizeof(info));
    }
    ret = (u32)(uintptr_t)vga->frambuffer;
  }
  return ret;
}

/* 【修复】驱动把真实 fb 几何登记给 VM 层（vma.c 原先硬编码 raspi2 的
 * 0xfb000000 / 1024*768*4；versatilepb(pl110) 的 fb 在 0xfb0000、640x480，
 * 用户进程此前拿到的是错误的 fb 窗口）。 */
extern void vmemory_set_fb(u32 addr, u32 size);

void gpu_init_device(device_t* dev) {
  vga_device_t* vga = kmalloc(sizeof(vga_device_t), DEFAULT_TYPE);
  vga->frambuffer = 0;
  dev->data = vga;
  // gpu_init_mode(vga, VGA_MODE_480x272x32);
  // gpu_init_mode(vga, VGA_MODE_640x480x32);
  gpu_init_mode(vga, VGA_MODE_1024x768x32);
  /* 驱动就绪：以最终生效值登记 fb 几何（pl110 会把请求的 mode 改写为
   * 其支持的 640x480，地址/尺寸以 vga 结构里的为准） */
  if (vga->frambuffer != 0 && vga->width > 0 && vga->height > 0) {
    vmemory_set_fb((u32)(uintptr_t)vga->frambuffer,
                   (u32)vga->width * (u32)vga->height * 4);
  }
  log_info("gpu_init_device end\n");
}

int gpu_init(void) {
  device_t* dev = kmalloc(sizeof(device_t), DEFAULT_TYPE);
  dev->name = "vga";
  dev->read = gpu_read;
  dev->write = gpu_write;
  dev->ioctl = gpu_ioctl;
  dev->id = DEVICE_VGA;
  dev->type = DEVICE_TYPE_VGA;
  device_add(dev);

  gpu_init_device(dev);

  // frambuffer
  device_t* fb_dev = device_find(DEVICE_VGA);
  if (fb_dev == NULL) {
    fb_dev = device_find(DEVICE_VGA_QEMU);
  }
  if (fb_dev != NULL) {
    vnode_t* frambuffer = vfs_create_node("fb", V_FILE);
    vfs_mount(NULL, "/dev", frambuffer);
    frambuffer->device = fb_dev;
    frambuffer->op = &device_operator;
  } else {
    log_error("dev fb not found\n");
  }

  return 0;
}

void gpu_exit(void) { log_info("gpu exit\n"); }

module_t gpu_module = {.name = "vga", .init = gpu_init, .exit = gpu_exit};