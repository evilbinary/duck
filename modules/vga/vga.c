/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "vga.h"
#include "dev/devfs.h"
#include "dma/dma.h"
#include "kernel/page.h"

size_t vga_read(device_t* dev, void* buf, size_t len) {
  u32 ret = 0;
  return ret;
}

size_t vga_write(device_t* dev, const void* buf, size_t len) {
  u32 ret = 0;
  vga_device_t* vga = dev->data;
  if (vga == NULL) {
    log_error("not found vga\n");
    return ret;
  }
  u8* dst = (u8*)vga->frambuffer;
  const u8* src = (const u8*)buf;
  for (size_t i = 0; i < len; i++) {
    dst[i] = src[i];
  }
  return len;
}

size_t vga_ioctl(device_t* dev, u32 cmd, ...) {
  size_t ret = 0;
  va_list args;
  va_start(args, cmd);
  void* arg = va_arg(args, void*);
  vga_device_t* vga = dev->data;
  if (vga == NULL) {
    log_error("not found vga\n");
    va_end(args);
    return ret;
  }
  if (cmd == VGA_IOC_READ_FRAMBUFFER) {
    ret = (size_t)vga->frambuffer;
  } else if (cmd == VGA_IOC_READ_FRAMBUFFER_WIDTH) {
    ret = vga->width;
  } else if (cmd == VGA_IOC_READ_FRAMBUFFER_HEIGHT) {
    ret = vga->height;
  } else if (cmd == VGA_IOC_READ_FRAMBUFFER_BPP) {
    ret = vga->bpp;
  } else if (cmd == VGA_IOC_FLUSH_FRAMBUFFER) {
    if (vga->frambuffer != NULL && vga->flip_buffer != NULL) {
      u32 offset = (u32)(size_t)arg;
      vga->flip_buffer(vga, offset % vga->framebuffer_count);
    }
  } else if (cmd == VGA_IOC_READ_FRAMBUFFER_INFO) {
    vga_device_t* buffer_info = (vga_device_t*)arg;
    *buffer_info = *vga;
  }
  va_end(args);
  return ret;
}

void vga_init_device(device_t* dev) {
  pci_device_t* pdev = pci_find_class(0x300);
  if (pdev == NULL) {
    log_error("can not find pci vga device\n");
    return;
  }
  u32 bar0 = pci_dev_read32(pdev, PCI_BASE_ADDR0) & 0xFFFFFFF0;
  // u32 bar1 =
  //     pci_read32(pdev->bus, pdev->slot, pdev->function, 0x14) & 0xFFFFFFF0;
  // u32 bar2 =
  //     pci_read32(pdev->bus, pdev->slot, pdev->function, 0x18) & 0xFFFFFFF0;

  // kprintf("bar0:%x ", bar0);
  vga_device_t* vga = kmalloc(sizeof(vga_device_t),DEFAULT_TYPE);
  vga->frambuffer = (u32*)bar0;
  dev->data = vga;
  u32 addr = bar0;
  for (int i = 0; i < 128; i++) {
    page_map(addr, addr, PAGE_P | PAGE_USR | PAGE_RWX);
    addr += 0x1000;
  }

  // vga_init_mode(vga,VGA_MODE_80x25);
  vga_init_mode(vga, VGA_MODE_320x200x256);
}

void vga_init(void) {
  device_t* dev = kmalloc(sizeof(device_t),DEFAULT_TYPE);
  dev->name = "vga";
  dev->read = vga_read;
  dev->write = vga_write;
  dev->ioctl = vga_ioctl;
  dev->id = DEVICE_VGA;
  dev->type = DEVICE_TYPE_VGA;
  device_add(dev);

  vga_init_device(dev);

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

}

void vga_exit(void) { log_debug("vga exit\n"); }

module_t vga_module = {.name = "vga", .init = vga_init, .exit = vga_exit};
