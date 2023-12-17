/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "kernel/kernel.h"
#include "sdhci.h"

int sdhci_dev_port_read(sdhci_device_t *sdhci_dev, char *buf, u32 len) {
  u32 ret = 0;
  return ret;
}

int sdhci_dev_port_write(sdhci_device_t *sdhci_dev, char *buf, u32 len) {
  return 0;
}

void sdhci_dev_init(sdhci_device_t *sdhci_dev) {}
