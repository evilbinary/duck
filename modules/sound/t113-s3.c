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
  void* phys = kpage_v2p(buf, 0);
  if (phys == NULL) {
    kprintf("phys is null\n");
    return;
  }

  // cpu_delay_msec(10);
  // AC_DAC_TXDATA
  u32* dac_txdata = CODEC_BASE + 0x0020;
  // io_write32(dac_txdata, buf);
  dma_trans(0, 0, phys, dac_txdata, len);

  // kprintf("dma trans end %x %d\n", phys, len);
}

void audio_ccu() {
  // a. AUDIO_CODEC_BGR_REG  ccu  0x0A5C
  u32 val;
  // open codec bus clock gating
  val = io_read32(CCU_BASE + 0x0A5C);
  val |= 1 << 0;  // AUDIO_CODEC_GATING PASS
  io_write32(CCU_BASE + 0x0A5C, val);

  // de-assert bug reset
  val = io_read32(CCU_BASE + 0x0A5C);
  val |= 1 << 16;  // AUDIO_CODEC_RST  De-assert
  io_write32(CCU_BASE + 0x0A5C, val);

  // b. AUDIO_CODEC_DAC_CLK_REG  ccu 0x0A50
  val = io_read32(CCU_BASE + 0x0A50);  // AUDIO_CODEC_DAC_CLK = Clock
                                       // Source/M/N.
  val |= 0 << 24;  // 00: PLL_AUDIO0(1X) 10: PLL_AUDIO1(DIV5)
  val |= 0 << 8;   // 00: /1 FACTOR_N
  val |= 0 << 0;   // FACTOR_M
  io_write32(CCU_BASE + 0x0A50, val);

  val |= 1 << 31;  // AUDIO_CODEC_DAC_CLK_GATING
  io_write32(CCU_BASE + 0x0A50, val);

  log_info("codec init1\n");

  // c. PLL_Audio0 frequency   PLL_AUDIO0_CTRL_REG  ccu 0x0078
  val = io_read32(CCU_BASE + 0x0078);
  // PLL_AUDIO0(1X) = (24MHz*N/M1/M0)/P/4
  val |= 4 << 16;  // PLL_P
  val |= 39 << 8;  // PLL_N
  val |= 0 << 1;   // PLL_M1
  val |= 1 << 0;   // PLL_M0
  val |= 1 << 30;  // PLL_LDO_EN
  val |= 1 << 27;  // PLL_OUTPUT_GATE
  val |= 1 << 24;  // PLL_SDM_EN
  // enable PLL_AUDIO0
  val |= 1 << 31;  // PLL_EN
  io_write32(CCU_BASE + 0x0078, val);

  // play back
  //  c. PLL_Audio1 PLL_AUDIO1 frequency   PLL_AUDIO1_CTRL_REG  ccu 0x0080
  val = io_read32(CCU_BASE + 0x0080);
  //   PLL_AUDIO1 = 24MHz*N/M 3072MHz
  val |= 4 << 16;  // PLL_P
  val |= 39 << 8;  // PLL_N
  val |= 0 << 0;   // PLL_M0
  val |= 1 << 27;  // PLL_OUTPUT_GATE 1: Enable
  val |= 1 << 30;  // PLL_LDO_EN
  val |= 0 << 24;  // PLL_SDM_EN
  // enable PLL_AUDIO1
  val |= 1 << 31;  // PLL_EN
  io_write32(CCU_BASE + 0x0080, val);

  log_info("codec init2\n");

  // PLL_AUDIO_PAT0_CTRL_REG 0x178
  // val = io_read32(CCU_BASE + 0x178);
  // val |= 1 << 31;  // SIG_DELT_PAT_EN
  // val |= 1 << 29;  // SPR_FREQ_MODE
  // val |= 0 << 20;  // WAVE_STEP
  // val |= 0 << 19;  // SDM_CLK_SEL
  // val |= 0x1EB85;  // WAVE_BOT
  // io_write32(CCU_BASE + 0x178, val);

  // PLL_AUDIO_PAT1_CTRL_REG 0x17C
  // io_write32(CCU_BASE + 0x17C, 0x0);

  // PLL_AUDIO_BIAS_REG 0x378
  // io_write32(CCU_BASE + 0x378, 0x00030000);

  /* Wait pll stable */
  val = io_read32(CCU_BASE + 0x0078);
  val |= (1 << 29);
  io_write32(CCU_BASE + 0x0078, val);

  while (!(io_read32(CCU_BASE + 0x0078) & (0x1 << 28)));

  val = io_read32(CCU_BASE + 0x0080);
  val |= (1 << 29);
  io_write32(CCU_BASE + 0x0080, val);

  while (!(io_read32(CCU_BASE + 0x0080) & (0x1 << 28)));
}

void codec_dac() {
  u32 val;

  // DAC_FIFOC
  val = io_read32(CODEC_BASE + 0x0010);

  val |= 0 << 29;  // DAC_FS  000: 48KHz 010: 24KHz 001: 32KHz
  val |=
      1 << 6;  // DAC_MONO_EN 0: Stereo, 64 Levels FIFO 1: Mono, 128 Levels FIFO
  val |= 0 << 5;   // TX_SAMPLE_BITS 0: 16 bits 1: 24 bits
  val |= 1 << 24;  // FIFO_MODE
  val |= 0 << 4;   // DAC FIFO Empty DRQ enable clear fifo
  val |= 1 << 3;   // DAC_IRQ_EN
  val |= 1 << 2;   // FIFO_UNDERRUN_IRQ_EN
  val |= 1 << 1;   // FIFO_OVERRUN_IRQ_EN

  val |= 0 << 8;   // TX_TRI_LEVEL
  val |= 0 << 28;  // FIR_VER
  val |= 1 << 26;  // SEND_LASAT
  val |= 0 << 21;  // DAC_DRQ_CLR_CNT
  val |= 1 << 0;   // FIFO_FLUSH elf clear to ‘0’
  io_write32(CODEC_BASE + 0x0010, val);

  // DAC_FIFOC
  // val = io_read32(CODEC_BASE + 0x0010);

  // val |= 0 << 29;  // DAC_FS   010: 24KHz 001: 32KHz 000: 48KHz
  // val |=
  //     1 << 6;  // DAC_MONO_EN 0: Stereo, 64 Levels FIFO 1: Mono, 128 Levels
  //     FIFO
  // val |= 1 << 5;   // TX_SAMPLE_BITS 0: 16 bits 1: 24 bits
  // val |= 1 << 24;  // FIFO_MODE
  // val |= 1 << 4;   // DAC FIFO Empty DRQ enable clear fifo
  // val |= 0 << 8;   // TX_TRI_LEVEL
  // val |= 1 << 28;  // FIR_VER
  // val |= 1 << 26;  // SEND_LASAT
  // val |= 3 << 21;  // DAC_DRQ_CLR_CNT
  // val |= 1 << 0;   // FIFO_FLUSH
  // io_write32(CODEC_BASE + 0x0010, val);

  log_info("codec init3.1 %x\n", io_read32(CODEC_BASE + 0x0010));

  // AC_DAC_DPC
  val = io_read32(CODEC_BASE + 0);
  val |= 1 << 31;  // DAC_EN
  val |= 0 << 12;  // DVOL
  val |= 1 << 18;  // HPF_EN
  val |= 1 << 0;   // HUB_EN
  io_write32(CODEC_BASE + 0, val);
  log_info("codec init3.2 %x\n", io_read32(CODEC_BASE + 0));

  // volumn DAC_VOL_CTRL 4
  val = io_read32(CODEC_BASE + 4);
  val |= 1 << 16;    // DAC_VOL_SEL
  val |= 0xa0 << 8;  // DAC_VOL_L 0xA0 = 0 dB 0xFF = 71.25 dB
  val | 0xa0 << 0;   // DAC_VOL_R 0xA0 = 0 dB 0xFF = 71.25 dB
  io_write32(CODEC_BASE + 4, val);
}

void codec_analog() {
  u32 val;
  // DAC  DAC Analog Control
  val = io_read32(CODEC_BASE + 0x0310);

  val |= 1 << 20;  // IOPVRS 01: 7 uA
  val |= 1 << 15;  // DACL_EN
  val |= 1 << 14;  // DACR_EN
  val |= 1 << 17;  // IOPDACS

  // val |= 1 << 10;    // DACR_MUTE ?
  // val |= 0x1f << 0;  // LINEOUT_VOL?
  // val |= 1 << 5;     // LINEOUTRDIFFEN?
  // val |= 1 << 6;     // LINEOUTLDIFFEN?

  // val |= 1 << 11;  // LINEOUTR_EN ?
  // val |= 1 << 12;  // DACLMUTE ?
  // val |= 1 << 13;  // LINEOUTL_EN ?
  io_write32(CODEC_BASE + 0x0310, val);

  // POWER Analog Control
  val = io_read32(CODEC_BASE + 0x0348);
  val |= 1 << 30;  // HPLDO_EN
  val |= 1 << 31;  // ALDO_EN
  val |= 3 << 12;  // ALDO_OUTPUT_VOLTAGE 011: 1.80 V
  val |= 3 << 8;   // HPLDO_OUTPUT_VOLTAGE 011: 1.80 V
  val |= 19 << 0;  // BG_TRIM
  io_write32(CODEC_BASE + 0x0348, val);

  // RAMP_REG Ramp Control Register
  val = io_read32(CODEC_BASE + 0x031C);
  val |= 1 << 0;    // RD_EN
  val |= 1 << 1;    // RMC_EN
  val |= 24 << 16;  // RAMP_CLK_DIV_M
  io_write32(CODEC_BASE + 0x031C, val);

  // HP2_REG
  val = io_read32(CODEC_BASE + 0x0340);
  val |= 1 << 31;  // HPFB_BUF_EN
  val |= 1 << 17;  // HPFB_IN_EN
  val |= 1 << 15;  // RAMP_OUT_EN
  val |= 1 << 19;  // RSWITCH
  val |= 1 << 21;  // HP_DRVEN
  val |= 1 << 20;  // HP_DRVOUTEN

  val |= 1 << 26;  // HPFB_RES
  val |= 1 << 24;  // OPDRV_CUR
  val |= 1 << 22;  // IOPHP
  val |= 2 << 13;  // RAMP_FINAL_STATE_RES

  io_write32(CODEC_BASE + 0x0340, val);

  // G
  val = io_read32(CODEC_BASE + 0x0324);
  val |= 1 << 15;  // G_EN
  val |= 1 << 10;  // HPOUTPUTEN
  val |= 1 << 11;  // HPINPUTEN
  io_write32(CODEC_BASE + 0x0324, val);
}

void codec_enable() {
  u32 val;
  // enable dma int
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

  // val = io_read32(CODEC_BASE + 0x00F8);
  // val |= 1 << 31;  // ADC_DAP0_EN
  // val |= 1 << 27;  // ADC_DAP1_EN
  // io_write32(CODEC_BASE + 0x00F8, val);

  // DMA_EN_REG
  // 1<<0 //DMA Channel Enable

  // AC_DAC_DAP_CTR
  val = io_read32(CODEC_BASE + 0x00F0);
  val |= 1 << 28;  // DDAP_HPF_EN
  io_write32(CODEC_BASE + 0x00F0, val);

  // AC_DAC_DAP_CTR
  val = io_read32(CODEC_BASE + 0x00F0);
  val |= 1 << 31;  // DDAP_EN
  val |= 1 << 28;  // DDAP_HPF_EN
  io_write32(CODEC_BASE + 0x00F0, val);

  // AC_DAC_DRC_HHPFC
  val = 0xFFFAC1 >> 16;
  io_write32(CODEC_BASE + 0x0100, val);

  // AC_DAC_DRC_LHPFC
  val = 0xFFFAC1;
  io_write32(CODEC_BASE + 0x0104, val);

  // AC_DAC_DRC_CTRL
  val = io_read32(CODEC_BASE + 0x0108);
  // val |= 1 << 4;  // DAC_DRC_DETECT_NOISE_EN
  // val |= 1 << 3;  // DAC_DRC_SIGNAL_FUNC_SEL
  // val |= 1 << 6;  // DAC_DRC_GAIN_MAX_LIMIT_EN
  // val |= 1 << 5;  // DAC_DRC_GAIN_MIN_LIMIT_EN
  // val |= 1 << 7;  // DAC_DRC_DELAY_BUF_EN

  val |= 1 << 1;  // DAC_DRC_LT_EN
  val |= 1 << 0;  // DAC_DRC_ET_EN
  io_write32(CODEC_BASE + 0x0108, val);

  // AC_DAC_DAP_CTR
  val = io_read32(CODEC_BASE + 0x00F0);
  val |= 1 << 29;  // DDAP_DRC_EN
  io_write32(CODEC_BASE + 0x00F0, val);

  // DAC_FIFOC
  val = io_read32(CODEC_BASE + 0x0010);
  val |= 1 << 0;  // FIFO_FLUSH elf clear to ‘0’
  io_write32(CODEC_BASE + 0x0010, val);

  // AC_DAC_CNT clear zero
  io_write32(CODEC_BASE + 0x24, 0);

  // AC_DAC_FIFOS
  val = io_read32(CODEC_BASE + 0x0014);
  val |= 1 << 2;  // TXU_INT
  val |= 1 << 1;  // TXO_INT
  io_write32(CODEC_BASE + 0x0014, val);

  // AC_DAC_FIFOC
  val = io_read32(CODEC_BASE + 0x0010);
  val |= 1 << 4;  // DAC_DRQ_EN

  io_write32(CODEC_BASE + 0x0010, val);
}

void codec_debug() {
  u32 val;
  // debug AC_DAC_DG
  val = io_read32(CODEC_BASE + 0x0028);
  val |= 1 << 11;  // DAC_MODU_SELECT  DAC Modulator Debug Mode
  val |= 0 << 8;  // 1: CODEC clock from OSC (for Debug) 0: CODEC clock from PLL
  val |= 1 << 9;  // DAC_PATTERN_SELECT 01: -6 dB Sin wave
  val |= 0 << 0;  // 000: Disabled
  val |= 0 << 6;  // DA_SWP
  io_write32(CODEC_BASE + 0x0028, val);
}

void codec_init() {
  log_info("codec init %x\n", CODEC_BASE);
  u32 val;
  page_map(CODEC_BASE, CODEC_BASE, 0);
  // 1. config
  audio_ccu();
  // 2.  Configure the sample rate and data transfer format, then open the DAC.
  codec_dac();

  codec_analog();

  codec_enable();

  // gic_enable(0, IRQ_AUDIO_CODEC);
  // codec_debug();
}

void* audio_handler(interrupt_context_t* ic) {
  kprintf("audio hndler\n");
  u32 val=0;

  // AC_DAC_FIFOS
  val = io_read32(CODEC_BASE + 0x0014);

  if (val & (1 << 2)) {
    val |= 1 << 2;  // TXU_INT
    kprintf("over run\n");
  }
  if (val & (1 << 1)) {
    val |= 1 << 1;  // TXO_INT
    kprintf("under run\n");
  }
  io_write32(CODEC_BASE + 0x0014, val);

  gic_irqack(IRQ_AUDIO_CODEC);
  return NULL;
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

  exception_regist(EX_AUDIO, audio_handler);

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
