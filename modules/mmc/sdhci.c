/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "sdhci.h"

static size_t sdhci_read(device_t* dev, void* buf, size_t len) {
  if (len == 0) {
    return 0;
  }
  // kprintf("sdhci_read buf:%x len:%d\n", buf, len);
  u32 ret = 0;

  // read too small process
  char* origin_buf = buf;
  u32 origin_buf_size = len;
  char small_buf[BYTE_PER_SECTOR];

  if (origin_buf_size < BYTE_PER_SECTOR) {
    buf = small_buf;
    len = BYTE_PER_SECTOR;
    kmemset(small_buf, 0, BYTE_PER_SECTOR);
  }

  sdhci_device_t* sdhci_dev = dev->data;
  int no = dev->id - DEVICE_SATA;
  ret = sdhci_dev_port_read(sdhci_dev, no, buf, len);

  if (origin_buf_size < BYTE_PER_SECTOR) {
    kmemmove(origin_buf, small_buf, origin_buf_size);
    ret = origin_buf_size;
  }
  // kprintf("sdhci_read ret %d\n", ret);

  return ret;
}

static size_t sdhci_write(device_t* dev, void* buf, size_t len) {
  if (len == 0) {
    return 0;
  }
  sdhci_device_t* sdhci_dev = dev->data;
  int no = dev->id - DEVICE_SATA;
  u32 startl = sdhci_dev->offsetl / BYTE_PER_SECTOR;
  u32 starth = sdhci_dev->offseth / BYTE_PER_SECTOR;
  u32 count = len / BYTE_PER_SECTOR;
  u32 rest = len % BYTE_PER_SECTOR;
  if (rest > 0) {
    kprintf("sdhci write is more, may have error\n");
    count++;
  }
  u32 ret = 0;
  sector_t sector;
  sector.startl = startl;
  sector.starth = starth;
  ret = sdhci_dev_port_write(sdhci_dev, no, sector, count, buf);
  if (ret == 0) {
    return -1;
  }
  return count * BYTE_PER_SECTOR;
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
  dev->data = sdhci_dev;
  sdhci_dev_init(sdhci_dev);

  return 0;
}

void sdhci_exit(void) { log_info("sdhci exit\n"); }

module_t sdhci_module = {
    .name = "sdhci", .init = sdhci_init, .exit = sdhci_exit};
