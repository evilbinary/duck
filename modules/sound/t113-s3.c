/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "dev/devfs.h"
#include "dma/dma.h"
#include "gpio.h"
#include "kernel/kernel.h"

#define SAMPLE_RATE 44100
#define NUM_SAMPLES 10000
#define FREQUENCY 440  // 440 Hz
#define M_PI 3.1415926

static size_t read(device_t* dev, void* buf, size_t len) {
  u32 ret = 0;

  return ret;
}

static size_t write(device_t* dev, void* buf, size_t len) {
  u32 ret = len;

  sound_play(buf, len);
  return ret;
}

void sound_play(void* buf, size_t len) {
  void* phys = kpage_v2p(buf, len);

  // AC_DAC_TXDATA
  u32* dac_txdata = CODEC_BASE + 0x0020;
  // io_write32(dac_txdata, buf);
  dma_trans(0, 0, phys, dac_txdata, len);

  kprintf("dma trans end %x %d\n", phys, len);
}

void codec_init() {
  log_info("codec init %x\n", CODEC_BASE);
  u32 val;
  page_map(CODEC_BASE, CODEC_BASE, 0);
  // 1. config

  // a. AUDIO_CODEC_BGR_REG  ccu  0x0A5C

  // open codec bus clock gating
  val = io_read32(CCU_BASE + 0x0A5C);
  val |= 1 << 0;  // AUDIO_CODEC_GATING PASS
  io_write32(CCU_BASE + 0x0A5C, val);

  // de-assert bug reset
  val = io_read32(CCU_BASE + 0x0A5C);
  val |= 1 << 16;  // AUDIO_CODEC_RST  De-assert
  io_write32(CCU_BASE + 0x0A5C, val);

  // b. AUDIO_CODEC_DAC_CLK_REG  ccu 0x0A50
  val = io_read32(CCU_BASE + 0x0A50);
  val |= 0 << 24;  // 00: PLL_AUDIO0(1X)
  val |= 0 << 8;   // 00: /1 FACTOR_N
  val |= 0 << 0;   // FACTOR_M
  io_write32(CCU_BASE + 0x0A50, val);

  val |= 1 << 31;  // AUDIO_CODEC_DAC_CLK_GATING
  io_write32(CCU_BASE + 0x0A50, val);

  log_info("codec init1\n");

  // c. PLL_Audio0 frequency   PLL_AUDIO0_CTRL_REG  ccu 0x0078
  val = io_read32(CCU_BASE + 0x0078);
  // PLL_AUDIO0(1X) = (24MHz*N/M1/M0)/P/4
  val |= 0 << 16;  // PLL_P
  val |= 0 << 8;   // PLL_N
  val |= 0 << 0;   // PLL_M0
  // enable PLL_AUDIO0
  val |= 1 << 31;  // PLL_EN
  io_write32(CCU_BASE + 0x0078, val);

  // play back
  //  c. PLL_Audio1 PLL_AUDIO1 frequency   PLL_AUDIO1_CTRL_REG  ccu 0x0080
  val = io_read32(CCU_BASE + 0x0080);
  //   PLL_AUDIO1(1X) = (24MHz*N/M1/M0)/P/4
  val |= 0 << 16;  // PLL_P
  val |= 9 << 8;   // PLL_N
  val |= 0 << 0;   // PLL_M0
  val |= 0 << 27;  // PLL_OUTPUT_GATE 1: Enable

  // enable PLL_AUDIO1
  val |= 1 << 31;  // PLL_EN

  io_write32(CCU_BASE + 0x0080, val);

  log_info("codec init2\n");

  // PLL_AUDIO_PAT0_CTRL_REG 0x178

  io_write32(CCU_BASE + 0x178, 0xc001eb85);

  // PLL_AUDIO_PAT1_CTRL_REG 0x17C
  io_write32(CCU_BASE + 0x17C, 0x0);

  // PLL_AUDIO_BIAS_REG 0x378
  io_write32(CCU_BASE + 0x378, 0x00030000);

  // 2.  Configure the sample rate and data transfer format, then open the DAC.

  // 3. Configure the DMA and DMA request.

  // DMA_CFG_REG
  // //set source dest config

  // DMA_CLK_GATE

  // DMA_BGR_REG
  // 1<<0   //1: Pass DMA_GATING
  // 1<<16  //1: De-assert

  // DMA_IRQ_PEND_REG dma irq pend
  // 1<<1 //DMA0_PKG_IRQ_ PEND

  // DMA_IRQ_EN_REG  dma irq enable
  // 1<<1 //DMA0_PKG_IRQ_EN  DMA 0 Package End Transfer Interrupt Enable.

  // DMA_DESC_ADDR_REG dma chennal des addr =

  // DMA_CHL0->DES=  //DMA_DESC_ADDR

  // 4. Enable the DAC DRQ and DMA.

  // 222 AC_DAC_DAP_CTR

  // DAC
  val = io_read32(CODEC_BASE + 0x0310);
  val |= 1 << 15;  // DACL_EN
  val |= 1 << 14;  // DACR_EN
  io_write32(CODEC_BASE + 0x0310, val);

  // G
  val = io_read32(CODEC_BASE + 0x0324);
  val |= 1 << 15;  // G_EN
  val |= 1 << 10;  // HPOUTPUTEN
  val |= 1 << 11;  // HPINPUTEN
  io_write32(CODEC_BASE + 0x0324, val);

  // debug AC_DAC_DG
  // val = io_read32(CODEC_BASE + 0x0028);
  // val |= 1 << 11;  // DAC_MODU_SELECT  DAC Modulator Debug Mode
  // val |= 0 << 8;   // 1: CODEC clock from OSC (for Debug)
  // val |= 2 << 9;
  // val |= 1;
  // io_write32(CODEC_BASE + 0x0028, val);
  // log_info("codec init3.0 %x\n", io_read32(CODEC_BASE + 0x0028));

  // AC_DAC_FIFOC
  val = io_read32(CODEC_BASE + 0x0010);
  val |= 0 << 29;  // DAC_FS   010: 24KHz 001: 32KHz 000: 48KHz
  val |=
      1 << 6;  // DAC_MONO_EN 0: Stereo, 64 Levels FIFO 1: Mono, 128 Levels FIFO
  val |= 1 << 5;   // TX_SAMPLE_BITS 0: 16 bits 1: 24 bits
  val |= 1 << 24;  // FIFO_MODE
  io_write32(CODEC_BASE + 0x0010);

  log_info("codec init3  %x\n", io_read32(CODEC_BASE + 0x0010));

  // DAC_FIFOC
  val = io_read32(CODEC_BASE + 0x0010);
  val |= 1 << 4;    // DAC FIFO Empty DRQ enable clear fifo
  val |= 0xf << 8;  // TX_TRI_LEVEL
  val |= 1 << 28;   // FIR_VER
  val |= 1 << 26;   // SEND_LASAT
  val |= 3 << 21;   // DAC_DRQ_CLR_CNT
  val |= 1 << 0;    // FIFO_FLUSH
  io_write32(CODEC_BASE + 0x0010, val);

  // AC_DAC_CNT clear zero
  io_write32(CODEC_BASE + 0x24, 0);

  log_info("codec init3.1 %x\n", io_read32(CODEC_BASE + 0x0010));

  // AC_DAC_DPC
  val = io_read32(CODEC_BASE + 0);
  val |= 1 << 31;     // DAC_EN
  val |= 0x3f << 12;  // DVOL
  io_write32(CODEC_BASE + 0, val);

  log_info("codec init3.2 %x\n", io_read32(CODEC_BASE + 0));

  // volumn DAC_VOL_CTRL 4
  val = io_read32(CODEC_BASE + 4);
  val |= 1 << 16;    // DAC_VOL_SEL
  val |= 0x0a << 8;  // DAC_VOL_L 0xA0 = 0 dB 0xFF = 71.25 dB
  val | 0x0a << 7;   // DAC_VOL_R 0xA0 = 0 dB 0xFF = 71.25 dB
  io_write32(CODEC_BASE + 4, val);

  // DMA_EN_REG
  // 1<<0 //DMA Channel Enable

  // test data

  // sound_play(pcm_data, NUM_SAMPLES * sizeof(short));
  //   void* p = kmalloc(1024, DEVICE_TYPE);
  //   memset(p, 256, 1024);

  // #include "test.h"
  //   for (int i = 0; i < __test_pcm_len / 16 * 32; i++) {
  //     // cpu_delay(1000);
  //     // sound_play(p, 32);
  //     cpu_delay_msec(20);
  //     sound_play(&__test_pcm[i], 32);
  //   }
}

int sound_init(void) {
  log_info("sound init\n");
  device_t* dev = kmalloc(sizeof(device_t), DEFAULT_TYPE);
  dev->name = "sound";
  dev->read = read;
  dev->write = write;
  dev->id = DEVICE_SB;
  dev->type = DEVICE_TYPE_BLOCK;
  device_add(dev);

  log_info("sound init2\n");
  // dsp
  vnode_t* dsp = vfs_create_node("dsp", V_FILE | V_BLOCKDEVICE);
  dsp->device = device_find(DEVICE_SB);
  dsp->op = &device_operator;
  vfs_mount(NULL, "/dev", dsp);

  log_info("sound init3 %x\n", CODEC_BASE);

  codec_init();

  log_info("sound init end\n");

  return 0;
}

void sound_exit(void) { kprintf("sound exit\n"); }

module_t sound_module = {
    .name = "sound", .init = sound_init, .exit = sound_exit};
