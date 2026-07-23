/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef VGA_H
#define VGA_H

#include "kernel/kernel.h"
#include "pci/pci.h"
#define DOUBLE_BUFFER 1

enum VGA_MODE {
  VGA_MODE_80x25 = 1,
  VGA_MODE_80x160x16,
  VGA_MODE_128x128x16,
  VGA_MODE_320x200x256 ,
  VGA_MODE_480x272x24,
  VGA_MODE_480x272x32,
  VGA_MODE_480x272x16,
  VGA_MODE_480x272x18,
  VGA_MODE_640x480x24,
  VGA_MODE_640x480x32,
  VGA_MODE_1024x768x32
};

#define VGA_IOC_MAGIC 'v'
#define VGA_IOC_INIT _IO(VGA_IOC_MAGIC, 0)
#define VGA_IOC_READ_REG _IOW(VGA_IOC_MAGIC, 1, int)
#define VGA_IOC_WRITE_REG _IOW(VGA_IOC_MAGIC, 2, int)
#define VGA_IOC_READ_FRAMBUFFER _IOW(VGA_IOC_MAGIC, 3, int)
#define VGA_IOC_READ_FRAMBUFFER_WIDTH _IOW(VGA_IOC_MAGIC, 4, int)
#define VGA_IOC_READ_FRAMBUFFER_HEIGHT _IOW(VGA_IOC_MAGIC, 5, int)
#define VGA_IOC_READ_FRAMBUFFER_BPP _IOW(VGA_IOC_MAGIC, 6, int)
#define VGA_IOC_FLUSH_FRAMBUFFER _IOW(VGA_IOC_MAGIC, 7, int)
#define VGA_IOC_READ_FRAMBUFFER_INFO _IOW(VGA_IOC_MAGIC, 8, int)

typedef struct vga_device vga_device_t;

typedef void (*write_pixel_fn)(vga_device_t* vga, void* buf,u32 len);
typedef void (*flip_buffer_fn)(vga_device_t* vga,u32 index);


typedef struct vga_device {
  u32 width;
  u32 height;
  u32 bpp;
  u32 mode;
  u32* frambuffer;
  u32 framebuffer_count;
  u32 framebuffer_index;
  u32 framebuffer_length;
  u32 inited;
  write_pixel_fn write;
  flip_buffer_fn flip_buffer;
  u32 format;
  u32* priv;
  u32* pframbuffer;
} vga_device_t;

size_t vga_read(device_t* dev, void* buf, size_t len);
size_t vga_write(device_t* dev, const void* buf, size_t len);
size_t vga_ioctl(device_t* dev, u32 cmd, ...);
void vga_init_mode(vga_device_t* vga, int mode);

#endif