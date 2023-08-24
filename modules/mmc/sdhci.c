/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "sdhci.h"

static size_t sdhci_read(device_t* dev, void* buf, size_t len) {
  if (len <= 0 || buf == NULL) {
    return -1;
  }
  u32 ret = 0;
  sdhci_device_t* sdhci_dev = dev->data;
  ret = sdhci_dev_port_read(sdhci_dev, buf, len);
  return ret;
}

static size_t sdhci_write(device_t* dev, void* buf, size_t len) {
  if (len <= 0 || buf == NULL) {
    return -1;
  }
  u32 ret = 0;
  sdhci_device_t* sdhci_dev = dev->data;
  ret = sdhci_dev_port_write(sdhci_dev, buf, len);
  return ret;
}

static size_t sdhci_ioctl(device_t* dev, u32 cmd, ...) {
  u32 ret = 0;
  sdhci_device_t* sdhci_dev = dev->data;
  int no = dev->id - DEVICE_SATA;
  if (sdhci_dev == NULL) {
    kprintf("not found sdhci\n");
    return ret;
  }
  va_list ap;
  va_start(ap, cmd);
  u32 offset = va_arg(ap, u32);
  if (cmd == IOC_READ_OFFSET) {
    ret = sdhci_dev->offsetl | sdhci_dev->offseth << 32;
  } else if (cmd == IOC_WRITE_OFFSET) {
    sdhci_dev->offsetl = offset;
    // sdhci_dev->offseth = (u32*)va_arg(ap, u32);
  }
  va_end(ap);

  return ret;
}

int sdhci_init(void) {
  log_info("sdhci_init\n");
  device_t* dev = kmalloc(sizeof(device_t), DEFAULT_TYPE);
  dev->name = "sata";
  dev->read = sdhci_read;
  dev->write = sdhci_write;
  dev->ioctl = sdhci_ioctl;
  dev->id = DEVICE_SATA;
  dev->type = DEVICE_TYPE_BLOCK;
  device_add(dev);

  sdhci_device_t* sdhci_dev = kmalloc(sizeof(sdhci_device_t), DEFAULT_TYPE);
  sdhci_dev->offseth = 0;
  sdhci_dev->offsetl = 0;
  sdhci_dev->port = dev->id - DEVICE_SATA;
  dev->data = sdhci_dev;

  sdhci_dev->read_buf = kmalloc(BYTE_PER_SECTOR, DEVICE_TYPE);
  sdhci_dev->read_buf_size = BYTE_PER_SECTOR;

  sdhci_dev->write_buf = kmalloc(BYTE_PER_SECTOR, DEVICE_TYPE);
  sdhci_dev->write_buf_size = BYTE_PER_SECTOR;
  sdhci_dev_init(sdhci_dev);

  return 0;
}

void sdhci_exit(void) { log_info("sdhci exit\n"); }

module_t sdhci_module = {
    .name = "sdhci", .init = sdhci_init, .exit = sdhci_exit};
