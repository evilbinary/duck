#include "general.h"

int sdhci_dev_port_read(sdhci_device_t *sdhci_dev, int no, sector_t sector,
                        u32 count, u32 buf) {
  sdhci_general_t *pdat = (sdhci_general_t *)sdhci_dev->data;
  size_t buf_size = count * BYTE_PER_SECTOR;
  u32 ret = 0;

  log_info("sdhci_dev_port_read no %d sector %d count %d buf %x\n",no,sector,count,buf);

    return ret;
}

int sdhci_dev_port_write(sdhci_device_t *sdhci_dev, int no, sector_t sector,
                         u32 count, u32 buf) {
  int ret = 0;

  return ret;
}

void sdhci_dev_init(sdhci_device_t *sdhci_dev) {
  sdhci_general_t *pdat = kmalloc(sizeof(sdhci_general_t), DEFAULT_TYPE);
  sdhci_dev->data = pdat;
  pdat->file = "../image/disk.img";

  log_info("sdh dev init end\n");
}
