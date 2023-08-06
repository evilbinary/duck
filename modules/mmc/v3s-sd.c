#include "v3s-sd.h"

#define CMD_LOAD (1U << 31)
#define CMD_PRG_CLK (1U << 21)
#define CMD_SEND_INIT_SEQ (1U << 15)
#define CMD_WAIT_PRE_OVER (1U << 13)
#define CMD_STOP_CMD_FLAG (1U << 12)
#define CMD_STREAM (1U << 11)
#define CMD_TRANS_WRITE (1U << 10)
#define CMD_DATA_TRANS (1U << 9)
#define CMD_CHK_RESP_CRC (1U << 8)
#define CMD_LONG_RESP (1U << 7)
#define CMD_RESP_RCV (1U << 6)

#define RES_R1 (CMD_CHK_RESP_CRC | CMD_RESP_RCV)
#define RES_R2 (CMD_CHK_RESP_CRC | CMD_LONG_RESP | CMD_RESP_RCV)
#define RES_R3 (CMD_RESP_RCV)
#define RES_R4 (CMD_RESP_RCV)
#define RES_R5 (CMD_CHK_RESP_CRC | CMD_RESP_RCV)
#define RES_R6 (CMD_CHK_RESP_CRC | CMD_RESP_RCV)
#define RES_R7 (CMD_CHK_RESP_CRC | CMD_RESP_RCV)

typedef struct card {
  u32 rca;
  u32 cap;
  u32 ccs : 1;
  u32 det : 1;
} card_t;

card_t card;

void sd_deinit(void) {
  CCU->SDMMC0_CLK = 0;
  PF->CFG0 = 0;
  card.cap = 0;
  card.det = 0;
  delay(50);
}

static uint32_t pll_periph_get_freq(void) {
    uint32_t reg = CCU->PLL_PERIPH0_CTRL;
    uint32_t mul = (reg >> 8) & 0x1F;
    uint32_t div = (reg >> 4) & 0x3;
    return (24000000 * (mul + 1) / (div + 1));
}

u32 clk_sdc_config(u32 freq) {
    uint32_t in_freq = 0;
    uint32_t reg_val = (1 << 31);
    if(freq <= 24000000) {
        reg_val |= (0 << 24); // OSC24M
        in_freq = 24000000;
        log_debug("clk OSC24M %d\n",in_freq);
    } else {
        reg_val |= (1 << 24); // PLL_PERIPH
        in_freq = pll_periph_get_freq();
        log_debug("clk PLL_PERIPH %d\n",in_freq);
    }

    uint8_t div = in_freq / freq;
    if(in_freq % freq) div++;

    uint8_t prediv = 0;
    while(div > 16) {
        prediv++;
        if(prediv > 3) return 0;
        div = (div + 1) / 2;
    }

    /* determine delays */
    uint8_t samp_phase = 0;
    uint8_t out_phase  = 0;
    if(freq <= 400000) {
        out_phase  = 0;
        samp_phase = 0;
    } else if(freq <= 25000000) {
        out_phase  = 0;
        samp_phase = 5;
    } else if(freq <= 52000000) {
        out_phase  = 3;
        samp_phase = 4;
    } else { /* freq > 52000000 */
        out_phase  = 1;
        samp_phase = 4;
    }
    reg_val |= (samp_phase << 20) | (out_phase << 8);
    reg_val |= (prediv << 16) | ((div - 1) << 0);

    return reg_val;
}


void sd_init(void) {
  sd_deinit();
  PF->CFG0 = 0x222222;
  CCU->BUS_CLK_GATING0 |= (1 << 8);  // MMC0_GATING
  CCU->BUS_SOFT_RST0 |= (1 << 8);    // SD0_RST
  CCU->SDMMC0_CLK = 0x8003000F;
  CCU->SDMMC0_CLK = clk_sdc_config(400 * 1000);
  SD0->GCTL = 0x107;  //(1 << 0)|(1<<1)|(1<<2)
  delay(50);
}

int sd_card_detect(void) {
  u32 ris = SD0->RIS & ((1 << 31) | (1 << 30));
  log_debug("RIS===>%x\n",ris);
  SD0->RIS = ris;
  log_debug("RIS1===>%x\n",ris);
  if (ris & (1 << 31)) {
    sd_init();
  }
  if (ris & (1 << 30)) card.det = 1;
  return card.det;
}

static int wait_status(u32 mask, u32 event) {
  for (int ctr_us = 0; ctr_us < 3000000;) {
    if (!sd_card_detect()) return -1;
    if ((SD0->STA & mask) == event) return 0;
  }
  return -2;
}

static int wait_event(u32 event) {
  u32 isr;
  for (int ctr_us = 0; ctr_us < 100000;) {
    isr = SD0->RIS;
    if (isr & (1 << 31)) {
      sd_init();
      return -1;
    }
    if (isr & event) return 0;
  }
  return -2;
}

static int cmd(u32 cmd, u32 arg) {
  u32 res;
  if (!card.det) return -1;
  SD0->ARG = arg;
  SD0->CMD = cmd | CMD_LOAD;
  res = wait_event(4);
  SD0->RIS = 0xFFFFFFFF;
  return res;
}

int sd_card_init(void) {
  int timeout;
  u32 arg;
  if (!card.cap) {
    SD0->CKC = 0;
    cmd(CMD_PRG_CLK | CMD_WAIT_PRE_OVER, 0);
    delay(10);
    SD0->CKC = 0x10000;
    cmd(CMD_PRG_CLK | CMD_WAIT_PRE_OVER, 0);
    SD0->BWD = 0;
    card.rca = 0;
    card.ccs = 0;
    timeout = 200;
    do {
      delay(1);
      cmd(CMD_SEND_INIT_SEQ, 0);  // GO_IDLE_STATE
      cmd(8 + RES_R7, 0x1AA);     // SEND_IF_COND
    } while (card.det && --timeout && (SD0->RESP0 & 0xFF) != 0xAA);
    arg = (SD0->RESP0 & 0xFF) == 0xAA ? 0x40FF8000 : 0x00FF8000;
    timeout = 1300;
    do {
      delay(1);
      cmd(55 + RES_R1, card.rca);
      cmd(41 + RES_R3, arg);  // SD_SEND_OP_COND
      if (--timeout == 0) {
        log_error("sd init timer out\n");
        return 0;  //{ puts("timeout"); return 0; }
      }
    } while (card.det && !(SD0->RESP0 & 0x80000000));
    if (SD0->RESP0 & 0x40000000 && arg & 0x40000000) card.ccs = 1;
    cmd(2 + RES_R2, 0);  // ALL_SEND_CID
    cmd(3 + RES_R6, 0);  // SEND_RELATIVE_ADDR
    card.rca = SD0->RESP0 & 0xFFFF0000;
    cmd(9 + RES_R2, card.rca);  // SEND_CSD
    if (SD0->RESP3 & 0x40000000) {
      arg = (SD0->RESP1 >> 16) | ((SD0->RESP2 & 0x3F) << 16);
      arg = (arg + 1) << 10;
    } else {
      arg = (SD0->RESP1 >> 30) | ((SD0->RESP2 & 0x3FF) << 2);
      arg = (arg + 1) << (((SD0->RESP1 & 0x38000) >> 15) + 2);
      arg <<= (((SD0->RESP2 >> 16) & 15) - 9);
    }
    cmd(7 + RES_R1,
        card.rca);          // SELECT/DESELECT_CARD (Standby to Transfer State)
    cmd(16 + RES_R1, 512);  // SET_BLOCKLEN
    cmd(55 + RES_R1, card.rca);
    cmd(6 + RES_R1, 2);  // SET_BUS_WIDTH (4bit SD bus)
    SD0->BWD = 1;
    CCU->SDMMC0_CLK = 0x80000000;  // Speed up
    log_info("===> %x %x\n", card.det, arg);
    card.cap = card.det ? arg : 0;
    delay(10);
  }
  return card.cap;
}

int sd_read(void *ptr, u32 addr, u32 cnt) {
  u32 ctr = cnt * 128, *buf = (u32 *)ptr;
  if (!card.cap) return cnt;
  if ((u32)ptr & 3) {
    // printf("sd_read: ptr is not aligned (%p %u %u)\n", ptr, addr, cnt);
    buf = kmalloc(cnt * 512, DEVICE_TYPE);
    sd_read(buf, addr, cnt);
    kmemcpy(ptr, buf, cnt * 512);
    kfree(buf);
    return cnt;
  }
  SD0->GCTL &= ~0x100;
  SD0->BYC = cnt * 512;
  SD0->ARG = card.ccs ? addr : addr * 512;
  SD0->GCTL |= (1U << 31);
  SD0->CMD = cnt == 1
                 ? 17 | CMD_DATA_TRANS | CMD_WAIT_PRE_OVER | CMD_LOAD | RES_R1
                 : 18 | CMD_DATA_TRANS | CMD_STOP_CMD_FLAG | CMD_WAIT_PRE_OVER |
                       CMD_LOAD | RES_R1;
  do {
    if (wait_status(4, 0)) break;
    *buf++ = SD0->FIFO;
  } while (--ctr);
  wait_event(4);
  wait_event(cnt == 1 ? 1 << 3 : 1 << 14);
  SD0->RIS = 0xFFFFFFFF;
  SD0->GCTL |= 0x100;
  return cnt;
}

int sd_write(void *ptr, u32 addr, u32 cnt) {
  uint32_t ctr = cnt * 128, *buf = (uint32_t *)ptr;
  if (!card.cap) return cnt;
  if ((u32)ptr & 3) {
    // printf("sd_write: ptr is not aligned (%p %u %u)\n", ptr, addr, cnt);
    buf = kmalloc(cnt * 512, DEVICE_TYPE);
    kmemcpy(buf, ptr, cnt * 512);
    sd_write(buf, addr, cnt);
    kfree(buf);
    return cnt;
  }
  SD0->GCTL &= ~0x100;
  SD0->BYC = cnt * 512;
  SD0->ARG = card.ccs ? addr : addr * 512;
  SD0->GCTL |= (1U << 31);
  SD0->CMD = cnt == 1
                 ? 24 | CMD_DATA_TRANS | CMD_WAIT_PRE_OVER | CMD_TRANS_WRITE |
                       CMD_LOAD | RES_R1
                 : 25 | CMD_DATA_TRANS | CMD_STOP_CMD_FLAG | CMD_WAIT_PRE_OVER |
                       CMD_TRANS_WRITE | CMD_LOAD | RES_R1;
  do {
    if (wait_status(8, 0)) break;
    SD0->FIFO = *buf++;
  } while (--ctr);
  wait_event(4);
  wait_event(cnt == 1 ? 1 << 3 : 1 << 14);
  SD0->RIS = 0xFFFFFFFF;
  SD0->GCTL |= 0x100;
  for (int ctr_us = 0; ctr_us < 10000000;) {
    cmd(13 + RES_R1, card.rca);
    if ((SD0->RESP0 & 0xF00) == 0x900) return cnt;
  }
  return cnt;
}

void sd_detect_thread() {
  while (1) {
    if (sd_card_detect()) {
      kprintf("Card inserted: %uMB\n", sd_card_init() / 2048);
      kprintf("SD-disk mount: ");
    } else {
      kprintf("Card removed");
    }
  }
}

int sdhci_dev_port_read(sdhci_device_t *sdhci_dev, int no, sector_t sector,
                        u32 count, u32 buf) {
  // kprintf("sdhci_dev_port_read sector:%d count:%d buf:%x\n", sector.startl,
  //         count, buf);
  u32 ret = 0;
  size_t buf_size = count * BYTE_PER_SECTOR;

#ifdef CACHE_ENABLED
  if (count == CACHE_COUNT) {
    int index = sector.startl & CACHE_MASK;
    void *cache_p = (void *)(sdhci_dev->cache_buffer + SECTOR_SIZE * index);
    if (sdhci_dev->cached_blocks[index] != sector.startl) {
      ret = sd_read(cache_p, sector.startl, count);
      sdhci_dev->cached_blocks[index] = sector.startl;
    }
    kmemmove(buf, cache_p, SECTOR_SIZE);
  } else {
#endif
    int index = sector.startl & CACHE_MASK;
    void *cache_p = (void *)(sdhci_dev->cache_buffer + SECTOR_SIZE * index);

    ret = sd_read(buf, sector.startl, count);
    sdhci_dev->cached_blocks[index] = sector.startl;
    kmemmove(cache_p, buf, SECTOR_SIZE);

    if (ret < count) {
      kprintf("mm read failed ret:%d\n", ret);
      return -1;
    }
#ifdef CACHE_ENABLED
  }
#endif
  // kprintf("sd read offset:%x %x buf_size:%d\n", sector.startl *
  // BYTE_PER_SECTOR,
  //         sector.starth * BYTE_PER_SECTOR, buf_size);
  // print_hex(buf, buf_size);
  // kprintf("ret %d\n", ret);
  return ret;
}

int sdhci_dev_port_write(sdhci_device_t *sdhci_dev, int no, sector_t sector,
                         u32 count, u32 buf) {
  u32 ret = 0;
  size_t buf_size = count * BYTE_PER_SECTOR;
  ret = sd_write(buf, sector.startl, count);
  if (ret < buf_size) {
    return -1;
  }
#ifdef CACHE_ENABLED
  int i;
  u8 *p = buf;
  for (i = 0; i < count; i++) {
    int index = (sector.startl + i) & CACHE_MASK;
    void *cache_p = (void *)(sdhci_dev->cache_buffer + SECTOR_SIZE * index);

    kmemcpy(cache_p, &p[SECTOR_SIZE * i], SECTOR_SIZE);
  }
#endif
  return ret;
}

void sdhci_dev_init(sdhci_device_t *sdhci_dev) {
  // sd mmc0
  page_map(SD0, SD0, L2_NCNB);

  sd_init();
  int ret = sd_card_detect();
  if (!ret) {
    log_error("sdcard not detect\n");
  }

  int size = sd_card_init();
  log_info("sdcard with: %uMB\n", size / 2048);
}