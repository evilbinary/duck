/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "dev/devfs.h"
#include "dma/dma.h"
#include "gpio.h"
#include "kernel/kernel.h"
#include "sound.h"

#define SAMPLE_RATE 44100
#define NUM_SAMPLES 10000
#define FREQUENCY 440  // 440 Hz
#define M_PI 3.1415926
#define BUFFER_SIZE sizeof(int)  // 缓冲区大小

#define NUM_CHANNELS 1
#define DURATION 1
#define AMPLITUDE 32767

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define TRANS_CPU 1

#define SOUND_BUF_SIZE SAMPLE_RATE * 4

struct sample_rate {
  unsigned int rate;
  unsigned int bit;
};

struct sample_rate rate_tab[] = {
    {8000, 5},  {11025, 4},  {12000, 4},  {16000, 3}, {22050, 2},
    {24000, 2}, {32000, 1},  {44100, 0},  {48000, 0}, {88200, 7},
    {96000, 7}, {192000, 6}, {992000, 6},
};

static size_t read(device_t* dev, void* buf, size_t len) {
  u32 ret = 0;

  return ret;
}

static size_t write(device_t* dev, void* buf, size_t len) {
  u32 ret = len;

  sound_play(dev->data, buf, len);
  return ret;
}

static void print_hex(u8* addr, u32 size) {
  for (int x = 0; x < size; x++) {
    kprintf("%02x ", addr[x]);
    if (x != 0 && (x % 32) == 0) {
      kprintf("\n");
    }
  }
  kprintf("\n\r");
}

void sound_play(sound_device_t* dev, void* buf, size_t len) {
  void* phys = kpage_v2p(buf, 0);
  if (phys == NULL) {
    kprintf("phys is null\n");
    return;
  }
  u32 val = 0;

  // val = io_read32(CODEC_BASE + 0x0024);
  // kprintf("tx count %d len=%d\n",val,len);

  cpu_cache_flush_range(buf, (u32)buf + len);
#ifdef TRANS_CPU
  u32* dac_txdata = CODEC_BASE + 0x0020;

  u32* p = buf;
  for (int i = 0; i < len;) {
    val = io_read32(CODEC_BASE + 0x0014);
    while ((val & (1 << 23))) {
      io_write32(dac_txdata, *p++);
      io_write32(dac_txdata, *p++);
      i += 8;
      val = io_read32(CODEC_BASE + 0x0014);
    }
  }

#else
  kmemcpy(dev->sound_buf, phys, len);
  if (dev->is_play == 0) {
    kprintf("dma trans start\n");
    dma_trans(0, dev->sound_buf, CODEC_BASE + 0x0020, SOUND_BUF_SIZE);
    kprintf("dma trans start1\n");
    dev->is_play = 1;
  }
  kprintf("copy end %x %d\n", phys, len);
#endif
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
  val |= 1 << 31;                      // AUDIO_CODEC_DAC_CLK_GATING
  io_write32(CCU_BASE + 0x0A50, val);

  val |= 0 << 24;  // 00: PLL_AUDIO0(1X) 10: PLL_AUDIO1(DIV5)
  val |= 0 << 8;   // 00: /1 FACTOR_N
  val |= 0 << 0;   // FACTOR_M
  io_write32(CCU_BASE + 0x0A50, val);
  log_info("codec init1\n");

  // c. PLL_Audio0 frequency   PLL_AUDIO0_CTRL_REG  ccu 0x0078
  val = io_read32(CCU_BASE + 0x0078);

  // enable PLL_AUDIO0
  val |= 1 << 31;  // PLL_EN
  val |= 1 << 30;  // PLL_LDO_EN
  val |= 7 << 28;  // PLL_OUTPUT_GATE
  val |= 1 << 27;  // PLL_OUTPUT_GATE
  val |= 1 << 24;  // PLL_SDM_EN
  // PLL_AUDIO0(1X) = (24MHz*N/M1/M0)/P/4 (24000000 * 39 / 2 / 1) / 4 /
  // 4=29 250 000
  val |= 4 << 16;  // PLL_P
  val |= 39 << 8;  // PLL_N
  val |= 0 << 1;   // PLL_M1
  val |= 1 << 0;   // PLL_M0

  io_write32(CCU_BASE + 0x0078, val);

  // // play back
  // //  c. PLL_Audio1 PLL_AUDIO1 frequency   PLL_AUDIO1_CTRL_REG  ccu 0x0080
  // val = io_read32(CCU_BASE + 0x0080);
  // //   PLL_AUDIO1 = 24MHz*N/M 3072MHz
  // val |= 4 << 16;  // PLL_P
  // val |= 39 << 8;  // PLL_N
  // val |= 0 << 0;   // PLL_M0
  // val |= 1 << 27;  // PLL_OUTPUT_GATE 1: Enable
  // val |= 1 << 30;  // PLL_LDO_EN
  // val |= 0 << 24;  // PLL_SDM_EN
  // // enable PLL_AUDIO1
  // val |= 1 << 31;  // PLL_EN
  // io_write32(CCU_BASE + 0x0080, val);

  log_info("codec init2\n");

  // PLL_AUDIO_PAT0_CTRL_REG 0x178
  val = io_read32(CCU_BASE + 0x178);
  val |= 1 << 31;  // SIG_DELT_PAT_EN
  val |= 2 << 29;  // SPR_FREQ_MODE
  val |= 0 << 20;  // WAVE_STEP
  val |= 0 << 19;  // SDM_CLK_SEL
  val |= 0x1EB85;  // WAVE_BOT
  io_write32(CCU_BASE + 0x178, val);

  val = io_read32(CCU_BASE + 0x0080);
  val &= ~(1 << 29);
  val |= (0 << 29);
  io_write32(CCU_BASE + 0x0080, val);

  val = io_read32(CCU_BASE + 0x0080);
  val |= (1 << 29);
  io_write32(CCU_BASE + 0x0080, val);
  while (!(io_read32(CCU_BASE + 0x0080) & (0x1 << 28)))
    ;

  // // PLL_AUDIO_PAT1_CTRL_REG 0x17C
  // io_write32(CCU_BASE + 0x17C, 0x0);

  // // PLL_AUDIO_BIAS_REG 0x378
  // io_write32(CCU_BASE + 0x378, 0x00030000);

  /* Wait pll stable */
  val = io_read32(CCU_BASE + 0x0078);
  val |= (1 << 29);
  io_write32(CCU_BASE + 0x0078, val);

  while (!(io_read32(CCU_BASE + 0x0078) & (0x1 << 28)))
    ;

  cpu_delay_msec(20);
}

void codec_dac() {
  u32 val;

  // DAC_FIFOC
  val = io_read32(CODEC_BASE + 0x0010);

  val |= 0 << 29;  // DAC_FS  000: 48KHz 010: 24KHz 001: 32KHz
  val |= 0 << 28;  // FIR_VER
  val |= 0 << 26;  // SEND_LASAT
  val |= 0 << 24;  // FIFO_MODE 1 2 3
  val |= 0 << 21;  // DAC_DRQ_CLR_CNT
  val |= 32 << 8;  // TX_TRIG_LEVEL
  val |=
      1 << 6;  // DAC_MONO_EN 0: Stereo, 64 Levels FIFO 1: Mono, 128 Levels FIFO
  val |= 0 << 5;  // TX_SAMPLE_BITS 0: 16 bits 1: 20 bits
  val |= 0 << 4;  // DAC FIFO Empty DRQ enable clear fifo
  val |= 0 << 0;  // FIFO_FLUSH elf clear to ‘0’

  io_write32(CODEC_BASE + 0x0010, val);

  log_info("codec init3.1 %x\n", io_read32(CODEC_BASE + 0x0010));

  // AC_DAC_DPC
  val = io_read32(CODEC_BASE + 0);
  val |= 1 << 31;  // DAC_EN
  val |= 1 << 12;  // DVOL
  val |= 1 << 18;  // HPF_EN
  val |= 1 << 0;   // HUB_EN
  io_write32(CODEC_BASE + 0, val);
  log_info("codec init3.2 %x\n", io_read32(CODEC_BASE + 0));

  // volumn DAC_VOL_CTRL 4
  val = io_read32(CODEC_BASE + 4);
  val |= 1 << 16;    // DAC_VOL_SEL
  val |= 0xa0 << 8;  // DAC_VOL_L 0xA0 = 0 dB 0xFF = 71.25 dB
  val |= 0xa0 << 0;  // DAC_VOL_R 0xA0 = 0 dB 0xFF = 71.25 dB
  io_write32(CODEC_BASE + 4, val);
}

void codec_analog() {
  u32 val;
  // DAC  DAC Analog Control
  val = io_read32(CODEC_BASE + 0x0310);

  val |= 0 << 23;  // CURRENT_TEST_SELECT

  val |= 1 << 20;  // IOPVRS 01: 7 uA
  val |= 1 << 18;  // ILINEOUTAMPS
  val |= 1 << 17;  // IOPDACS
  val |= 1 << 15;  // DACL_EN
  val |= 1 << 14;  // DACR_EN

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
  val |= 1 << 31;    // ALDO_EN
  val |= 1 << 30;    // HPLDO_EN
  val |= 3 << 12;    // ALDO_OUTPUT_VOLTAGE 011: 1.80 V
  val |= 3 << 8;     // HPLDO_OUTPUT_VOLTAGE 011: 1.80 V
  val |= 0x19 << 0;  // BG_TRIM
  io_write32(CODEC_BASE + 0x0348, val);

  // RAMP_REG Ramp Control Register
  val = io_read32(CODEC_BASE + 0x031C);
  // val |= 1 << 0;    // RD_EN
  val |= 1 << 1;    // RMC_EN
  val |= 24 << 16;  // RAMP_CLK_DIV_M
  io_write32(CODEC_BASE + 0x031C, val);

  // HP2_REG
  val = io_read32(CODEC_BASE + 0x0340);
  val |= 1 << 31;  // HPFB_BUF_EN
  val |= 1 << 26;  // HPFB_RES
  val |= 2 << 24;  // OPDRV_CUR
  val |= 1 << 22;  // IOPHP
  val |= 1 << 21;  // HP_DRVEN
  val |= 1 << 20;  // HP_DRVOUTEN
  val |= 1 << 19;  // RSWITCH
  val |= 1 << 17;  // HPFB_IN_EN
  val |= 1 << 15;  // RAMP_OUT_EN
  val |= 2 << 13;  // RAMP_FINAL_STATE_RES

  io_write32(CODEC_BASE + 0x0340, val);

  // G
  val = io_read32(CODEC_BASE + 0x0324);
  val |= 1 << 15;  // G_EN
  val |= 1 << 10;  // HPOUTPUTEN
  val |= 1 << 11;  // HPINPUTEN
  io_write32(CODEC_BASE + 0x0324, val);
}

void codec_enable(int enable) {
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
  val |= 1 << 29;  // DDAP_DRC_EN //动态控制
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
  val |= 1 << 3;  // DAC_IRQ_EN
  val |= 1 << 2;  // FIFO_UNDERRUN_IRQ_EN
  val |= 1 << 1;  // FIFO_OVERRUN_IRQ_EN
  val |= 1 << 4;  // DAC_DRQ_EN
  io_write32(CODEC_BASE + 0x0010, val);

  // gic_irq_enable( IRQ_AUDIO_CODEC);
}

void codec_debug() {
  u32 val;
  // debug AC_DAC_DG
  val = io_read32(CODEC_BASE + 0x0028);
  val |= 1 << 11;  // DAC_MODU_SELECT  DAC Modulator Debug Mode
  val |= 1 << 8;  // 1: CODEC clock from OSC (for Debug) 0: CODEC clock from PLL
  val |= 1 << 9;  // DAC_PATTERN_SELECT 01: -6 dB Sin wave
  val |= 0 << 0;  // 000: Disabled
  val |= 0 << 6;  // DA_SWP
  io_write32(CODEC_BASE + 0x0028, val);
}

void codec_param(int format, int channal, int freq) {
  u32 val = 0;
  val = io_read32(CODEC_BASE + 0x0010);
  if (format == 16) {
    // DAC_FIFOC
    val &= ~(3 << 24);
    val |= 3 << 24;
    val &= ~(0 << 5);
    val |= (0 << 5);

  } else if (format == 24) {
    // DAC_FIFOC
    val &= ~(3 << 24);
    val |= 0 << 24;
    val &= ~(1 << 5);
    val |= (1 << 5);
  }

  if (channal == 1) {  // mono
    val &= ~(1 << 6);
    val |= (1 << 6);
  } else if (channal == 2) {  // stereo
    val &= ~(1 << 6);
    val |= (0 << 6);
  }
  val &= ~(7 << 29);
  val |= 0 << 29;
  if (freq > 0) {

    for (int i = 0; i < ARRAY_SIZE(rate_tab); i++) {
      kprintf("freq %d rate %d\n", freq, rate_tab[i].rate);
      if (freq >= rate_tab[i].rate) {
        val &= ~(7 << 29);
        val |= rate_tab[i].bit << 29;
        kprintf("set rate %d bit %x\n", rate_tab[i].rate, rate_tab[i].bit);
      }
    }
  }

  io_write32(CODEC_BASE + 0x0010, val);
}

void dma_audio_handler(void* data) {
  log_info("dma_audio_handler %x\n", data);

  dma_trans(0, data, CODEC_BASE + 0x0020, SOUND_BUF_SIZE);
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
  codec_param(16, 2, 44100);

  // codec_debug();

  codec_enable(1);

  // // 生成正弦波 PCM 数据
  // generate_sine_wave(pcm_data, SAMPLE_RATE);

  // while (1) {
  //   // sound_play(pcm_data, SAMPLE_RATE);
  //   sound_play(test_pcm, test_pcm_len);
  // }
}

int write_count = 0;

void* audio_handler(interrupt_context_t* ic) {
  kprintf("audio hndler\n");
  u32 val = 0;

  // AC_DAC_FIFOS
  val = io_read32(CODEC_BASE + 0x0014);

  u32 count = val >> 8 & 0xe;
  kprintf("count %d\n", count);

  // if (val & (1 << 2)) {
  //   val &= ~(1 << 2);
  //   kprintf("over run\n");
  //   io_write32(CODEC_BASE + 0x0014, val);

  //   while (!(io_read32(CODEC_BASE + 0x0014) & (1 << 2)));
  // }
  // if (val & (1 << 1)) {
  //   val &= ~(1 << 1);
  //   kprintf("under run\n");
  //   io_write32(CODEC_BASE + 0x0014, val);

  //   while (!(io_read32(CODEC_BASE + 0x0014) & (1 << 1)));
  // }

  if (val & (1 << 3)) {
    val &= ~(1 << 3);
    kprintf("irq TXE_INT\n");
    io_write32(CODEC_BASE + 0x0014, val);
    while (!(io_read32(CODEC_BASE + 0x0014) & (1 << 3)))
      ;
  }

  kprintf("irq TXE_INT1\n");

  // sound_play(pcm_data, SAMPLE_RATE);

  gic_irqack(IRQ_AUDIO_CODEC);

  return NULL;
}

size_t sound_ioctl(device_t* dev, u32 cmd, void* args) {
  u32 ret = 0;
  kprintf("sound_ioctl %d\n", cmd);

  if (cmd == SNDCTL_DSP_GETFMTS) {
    u32* val = args;
    *val = AFMT_S16_LE;
  } else if (cmd == SNDCTL_DSP_CHANNELS) {
    u32* val = args;
    kprintf("SNDCTL_DSP_CHANNELS %d\n", *val);
    codec_param(-1, *val, -1);

  } else if (cmd == SNDCTL_DSP_SPEED) {
    u32* val = args;
    kprintf("SNDCTL_DSP_SPEED %d\n", *val);
    codec_param(-1, -1, *val);

    kprintf("dma_init\n");
#ifdef TRANS_CPU
    dma_init(0, 0, dma_audio_handler);
#else
    dma_init(0, 1, dma_audio_handler);
#endif

    kprintf("dma_init end\n");
  }

  return ret;
}

int sound_init(void) {
  log_info("sound init\n");
  device_t* dev = kmalloc(sizeof(device_t), DEFAULT_TYPE);
  dev->name = "sound";
  dev->read = read;
  dev->write = write;
  dev->ioctl = sound_ioctl;
  dev->id = DEVICE_SB;
  dev->type = DEVICE_TYPE_BLOCK;
  device_add(dev);

  sound_device_t* sound_device = kmalloc(sizeof(sound_device_t), DEFAULT_TYPE);
  dev->data = sound_device;
  sound_device->sound_buf = kmalloc(SOUND_BUF_SIZE, DEVICE_TYPE);

  gic_irq_priority(0, IRQ_AUDIO_CODEC, 10);

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
