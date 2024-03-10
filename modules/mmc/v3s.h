#ifndef V3S_SDHC_H
#define V3S_SDHC_H

#include "sdhci.h"
#include "sunxi_sdhci.h"

static int sdhci_v3s_transfer(sdhci_device_t *sdhci, sdhci_cmd_t *cmd,
                              sdhci_data_t *dat);

#endif