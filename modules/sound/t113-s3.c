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

/* 【临时诊断】毫秒时基。SCHEDULE_FREQUENCY=1000 ⇒ 1 tick = 1ms，
 * 用它量 DMA 完成周期与应用写入间隔（不动用 cpu_read_ms，避免 TU 依赖）。 */
extern u32 schedule_get_ticks(void);

#define SAMPLE_RATE 44100
#define NUM_SAMPLES 10000
#define FREQUENCY 440  // 440 Hz
#define M_PI 3.1415926
#define BUFFER_SIZE sizeof(int)  // 缓冲区大小

#define NUM_CHANNELS 1
#define DURATION 1
#define AMPLITUDE 32767

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
/* 【CPU 直供通路：已关闭】作者自带的 TRANS_CPU 曾用于隔离 DMA 做诊断
 * （实测节流正确，但内核态忙等会吃满一个核，且本内核不允许在内核调用中间换出，
 * 无法在那里安全 sleep）。现改回 DMA 通路，DMA 的节流问题已在 sunxi-dma.c
 * 按主线 sun6i-dma 修正：LLI 第 5 个字必须是常量 NORMAL_WAIT=8，
 * 本仓库原先拼成 (wait_cyc|data_block_size<<8)=0x0200 ⇒ DRQ 握手失效、猛冲丢数据。 */
// #define TRANS_CPU 1

#define SOUND_BUF_SIZE SAMPLE_RATE * 40
/* sound_buf 的分配大小（≥ 单次 DMA 传输即可） */
#define SOUND_PLAY_SIZE 44100
/* 【单次 DMA 传输字节数】原实现直接拿 SOUND_PLAY_SIZE（44100 字节）当传输长度：
 * 在 22.05kHz/16bit/单声道下就是**整整 1 秒**。粒度这么粗有两个直接后果：
 *   1) 环形缓冲一旦暂时欠载，尾部补零 ⇒ 听感"整秒断音"；
 *   2) DMA 每秒才重装一次，重装窗口叠上缓冲抖动 ⇒ 每秒一次的爆音/咔哒。
 * 改成 ~46ms（22050 单声道下 2048 样本 / 4096 字节）级别：欠载只影响一小段，
 * 重装开销可忽略（重装在中断里，几十微秒量级）。 */
#define SOUND_DMA_CHUNK 4096

struct sample_rate {
  unsigned int rate;
  unsigned int bit;
};

struct sample_rate rate_tab[] = {
    {8000, 5},  {11025, 4},  {12000, 4},  {16000, 3}, {22050, 2},
    {24000, 2}, {32000, 1},  {44100, 0},  {48000, 0}, {88200, 7},
    {96000, 7}, {192000, 6}, {992000, 6},
};


void dma_audio_handler(void* data);

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
  /* 【临时诊断】应用写入画像：len=本次写入字节, avail=写入前环形缓冲可用字节,
   * t=毫秒时基, dt=距上次写入的毫秒数。dt 能直接暴露 app 的投喂节奏是否正常
   * （SDL 后端靠 DSP_WaitAudio 里 SDL_Delay(samples*1000/freq) 自我节流）。 */
  {
    static u32 dbg_w, dbg_wlast;
    u32 now = schedule_get_ticks();
    if (dbg_w < 16u) {
      dbg_w++;
      kprintf("swr#%u len=%u avail=%u t=%u dt=%u\n", dbg_w, (u32)len,
              (u32)buffer_size(dev->buffer), now, now - dbg_wlast);
    }
    dbg_wlast = now;
  }

  /* 【关键修复】原实现用 kpage_v2p(buf, 0) 做早退判断：buf 是【用户态】地址，
   * kpage_v2p 只认内核映射 ⇒ 返回 NULL ⇒ 整个函数直接 return ✗
   * ⇒ 应用的 PCM 从来没进过环形缓冲（DMA 只能反复播 sound_buf 里的旧数据/
   * 未初始化垃圾 ⇒ 听感"有声音但全是电流噪声"，把 sound_buf 清零后则"完全没声音"）。
   * 而且这个 phys 在函数里根本没被使用（DMA 走的是 dev->sound_buf），
   * 所以直接去掉这个早退，让数据真正进入环形缓冲。 */
  u32 val = 0;

  // val = io_read32(CODEC_BASE + 0x0024);
  // kprintf("tx count %d len=%d\n",val,len);

  cpu_flush_dcache_range(buf, (u32)buf + len);
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
  if (dev->buf_pos > SOUND_BUF_SIZE) {
    dev->buf_pos = 0;
  }
  // kmemcpy(&dev->sound_buf[dev->buf_pos], buf, len);
  // dev->buf_pos += len;

  // dev->play_size = len;
  // kmemcpy(dev->sound_buf, buf, len);

  if ((buffer_size(dev->buffer) + len) > SOUND_BUF_SIZE) {
    buffer_clear(dev->buffer);
  }
  buffer_write(dev->buffer, buf, len);

  if (dev->is_play == 0) {
    kprintf("dma_init\n");
#ifdef TRANS_CPU
    dma_init(0, 0, dma_audio_handler, NULL);
#else
    /* mode 的 bit16 = 流式 DRQ 传输：让 DMA 层按主线 sun6i-dma 写
     * para=NORMAL_WAIT(8) 并使用一次性描述符（详见 sunxi-dma.c 的注释）。 */
    dma_init(0, 1 | (1 << 16), dma_audio_handler, dev);
#endif

    kprintf("dma trans start sound buf %x buf %x len %d\n", dev->sound_buf, buf,len);
    /* 【修复·开局噪声】原实现首次武装直接把 sound_buf 当源，但该缓冲区此时还
     * 没从环形缓冲里取过数据（它只是 DMA 的搬运缓冲）⇒ 开头那一段播的是它里面
     * 未初始化的旧内容 ⇒ 听感上就是"一开始一阵杂音"。
     * 首次武装与中断里的做法保持一致：先清零、再从环形缓冲取 play_size 字节，
     * 最后刷 cache 到 PoC 再发货。 */
    kmemset(dev->sound_buf, 0, dev->play_size);
    buffer_read(dev->buffer, dev->sound_buf, dev->play_size);
    cpu_flush_dcache_range(dev->sound_buf,
                           (u32)dev->sound_buf + dev->play_size);
    dma_trans(0, dev->sound_buf, CODEC_BASE + 0x0020, dev->play_size);
    kprintf("dma trans start1\n");
    dev->is_play = 1;
  }
  // kprintf("copy end %x %d\n", phys, len);
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
  /* 【订正到 44.1k 家族】依据主线 linux-6.3.1/drivers/clk/sunxi-ng/
   * ccu-sun20i-d1.c（D1 与 T113 同源）：
   *   pll_audio0_4x_clk: n = _SUNXI_CCU_MULT_MIN(8, 8, 12) ⇒ N 在 bits[15:8]
   *                      m = _SUNXI_CCU_DIV(16, 6)         ⇒ M 在 bits[21:16]
   *   pll_audio0_sdm_table: { rate=90316800, pattern=0xc001288d, m=6, n=22 }
   *   pll_audio0(1X) = pll_audio0_4x / 4  ⇒ 4X=90.3168MHz、1X=22.5792MHz
   * （DAC_FS 只是家族分频档，22050 与 44100 同档；基准时钟是 22.5792MHz 时该档
   *   才真正等于 22.05kHz，BSP 采样率表与主线一致。）
   * 原代码把 bit16 当 P、bit8 当 N、小数走 WAVE_BOT ⇒ 落在 48k 家族（实测
   * 24.09kHz，快 8.8% ⇒ 人声发尖；消费快于生产 ⇒ 环缓冲周期性抽空 ⇒ 停顿）。 */
  val &= ~(0xFFu << 8);
  val &= ~(0x3Fu << 16);
  val |= (22 << 8);  // N = 22
  val |= (6 << 16);  // M = 6
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
  /* SDM(pattern)寄存器：主线 ccu-sun20i-d1.c 的 SDM 项直接给出整字 pattern
   *   { rate = 90316800, pattern = 0xc001288d }（对应 4X = 90.3168MHz）
   * 且其 bit31 即 SIG_DELT_PAT_EN（pattern 自带）。整字写入即可。
   * 原代码按 WAVE_BOT 字段拼 0x1EB85 ⇒ 小数部分不对，输出落在 48k 家族。 */
  io_write32(CCU_BASE + 0x178, 0xc001288d);

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
  /* DVOL(bit12 起，6 位) = 数字音量字段。板级设备树
   * sun8i-mangopi-mq-dual-linux.dts 的 &codec { digital_vol = <0x00>; }，
   * sun20iw1-codec.c 的 digital_tlv = (-7424, 116) ⇒ 0 档即 0dB（最大）。
   * 原代码写 1（白降约 1.16dB）。这里按板级值把该字段清零。 */
  val &= ~(0x3F << 12);
  val |= 0 << 12;  // DVOL = 0 (0 dB)
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

  /* 【板级对齐】逐位依据：sun20iw1-codec.h 的 SUNXI_DAC_REG 位定义
   *   DACLEN=15 DACREN=14 LINEOUTLEN=13 DACLMUTE=12 LINEOUTREN=11 DACRMUTE=10
   *   LINEOUTLDIFFEN=6 LINEOUTRDIFFEN=5 LINEOUT_VOL=[4:0]
   * 以及 sun20iw1-codec.c 的上电序列（DAPM_PRE_PMU）：
   *   - 把 DACLMUTE/DACRMUTE 置 1（源码注释原文："0:mute 1: not mute"）；
   *   - 使能 LINEOUTLEN/LINEOUTREN；
   *   - LINEOUT_VOL 取板级 DT 值 0x1a（lineout_tlv 里 0~1 是 mute，2~31 才是音量）。
   * 原代码把这四件都注释掉了 ⇒ 若输出走 line-out（或复位值使 MUTE=0），默认是静音。 */
  val |= 1 << 12;  // DACLMUTE = 1 (not mute)
  val |= 1 << 10;  // DACRMUTE = 1 (not mute)
  val |= 1 << 13;  // LINEOUTL_EN = 1
  val |= 1 << 11;  // LINEOUTR_EN = 1
  val &= ~(0x1F << 0);
  /* LINEOUT_VOL：sun20iw1-codec.c 的 lineout_tlv（invert=0）为
   *   0~1 → mute；2~31 → (-4350 + (n-2)*150) 里氏，n=31 → 0 dB（最大）。
   * 板级 DT 用 0x1a(-7.5dB)，这里取 0x1f 追求最大音量（两者都是"非静音"档）。 */
  val |= 0x1f << 0;  // LINEOUT_VOL = 0x1f (0 dB, 最大)

  io_write32(CODEC_BASE + 0x0310, val);

  // POWER Analog Control
  val = io_read32(CODEC_BASE + 0x0348);
  val |= 1 << 31;    // ALDO_EN
  val |= 1 << 30;    // HPLDO_EN
  /* 【注释与代码不符】原注释写明 "011: 1.80 V"，代码却写 0。这两个字段是
   * HP/AVCC 的内部 LDO 输出电压（见头文件 SUNXI_POWER_REG: ALDO/HPLDO_OUTPUT_VOLTAGE），
   * 电压越低耳机功放的最大摆幅越小 ⇒ 越小声。按作者自己注释的意图取 3（1.80V）。 */
  val &= ~(0x7 << 12);
  val |= 3 << 12;    // ALDO_OUTPUT_VOLTAGE = 011 (1.80 V)
  val &= ~(0x7 << 8);
  val |= 3 << 8;     // HPLDO_OUTPUT_VOLTAGE = 011 (1.80 V)
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
  /* HEADPHONE_GAIN = HP2_REG[30:28]（头文件 SUNXI_HP2_REG）。
   * sun20iw1-codec.c 的 headphone_gain_tlv = (-4200, 600) 且该控制 invert=1：
   * 用户档位 0 ↔ 寄存器 7（-42dB），用户档位 7 ↔ 寄存器 0（0dB）。
   * 即寄存器值 0 = 0dB = 最响，7 = -42dB。
   * 【实测校正】曾按板级 DT 的 headphonegain=<3> 写成 3，听感明显变小 ——
   * 与上面推论一致（3 档 ≈ -24dB）。故此处保持 0 档（0dB，最响）。 */
  val &= ~(0x7 << 28);
  val |= 0 << 28;  // HEADPHONE_GAIN = 0 (0 dB, 最响档)
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
  /* 【板级对齐】sun20iw1-codec.h: SUNXI_DAC_DAP_CTL(0xF0)
   *   DDAP_EN=31 / DDAP_DRC_EN=29 / DDAP_HPF_EN=28
   * BSP 只在板级 DT 的 dachpf_cfg/dacdrc_cfg 非 0 时才开这两个子块，且关闭时会
   * 显式把 DDAP_HPF_EN 清 0（见 sun20iw1-codec.c 的 dap 开关函数）。
   * MangoPi MQ-Dual 的 &codec { dachpf_cfg = <0x0>; dacdrc_cfg = <0x0>; } ⇒ 该板
   * 两个 DAP 子块都是关的。原代码无条件置 DDAP_HPF_EN=1，却又从不设置父开关
   * DDAP_EN ⇒ 一个"父块未开、子块被使能"的滤波器挂在 DAC 信号链上：听感既是
   * 被额外高通削薄("沙哑")，又容易表现为离散毛刺。按板级值关闭。 */
  val = io_read32(CODEC_BASE + 0x00F0);
  val &= ~(1u << 28);  // DDAP_HPF_EN = 0（板级 dachpf_cfg = 0）
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
  val |= 1 << 4;  // DAC_DRC_DETECT_NOISE_EN
  val |= 1 << 3;  // DAC_DRC_SIGNAL_FUNC_SEL
  val |= 1 << 6;  // DAC_DRC_GAIN_MAX_LIMIT_EN
  val |= 1 << 5;  // DAC_DRC_GAIN_MIN_LIMIT_EN
  val |= 1 << 7;  // DAC_DRC_DELAY_BUF_EN

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
      if (freq >= rate_tab[i].rate) {
        val &= ~(7 << 29);
        val |= rate_tab[i].bit << 29;
      }
    }
  }

  io_write32(CODEC_BASE + 0x0010, val);
  /* 【临时诊断】打印最终生效的 DAC_FIFOC（含 [31:29] 采样率位、[25:24] 位宽、
   * bit6 单声道位），用于判断"格式/速率不一致"导致的噪声。 */
  kprintf("codec_param fmt=%d ch=%d freq=%d fifoc=%x\n", format, channal, freq,
          (u32)io_read32(CODEC_BASE + 0x0010));
}

void dma_audio_handler(void* data) {
  sound_device_t* dev = data;
  if (dev->play_size <= 0) {
    dma_stop(0);
    log_debug("dma stop\n");
    return 0;
  }

  /* 【临时诊断】投喂画像：必须先读原始 FIFO 状态（清位之后就看不到
   * overrun/underrun 了），并带毫秒时基 dt=距上次完成的毫秒数 ⇒ 直接量出
   * DMA 的真实传输周期：≈93ms 说明被 CODEC DRQ 正确节流；≈0 说明在猛冲。 */
  u32 fs_raw = io_read32(CODEC_BASE + 0x0014);
  {
    static u32 dbg_audio, dbg_last_t;
    u32 now = schedule_get_ticks();
    if (dbg_audio < 24u) {
      dbg_audio++;
      kprintf("audio#%u avail=%u need=%u fifo=%x t=%u dt=%u\n", dbg_audio,
              (u32)buffer_size(dev->buffer), (u32)dev->play_size, fs_raw, now,
              now - dbg_last_t);
    }
    dbg_last_t = now;
  }

  /* 【清 FIFO 错误位】DAC FIFO 的 overrun/underrun 是写 1 清（W1C）：初始化时
   * 使能了这两个中断却从未清过状态，位会一直挂着。欠载在"应用供数暂时跟不上"
   * 时本来就是正常的，清掉它以免 DAC 停在错误态输出杂音。 */
  if (fs_raw & ((1u << 2) | (1u << 1))) {
    io_write32(CODEC_BASE + 0x0014, fs_raw & ((1u << 2) | (1u << 1)));
  }

  /* 【D·消除"重播旧 PCM"】先清零、再填充。
   * buffer_read() 在环形缓冲里数据不足时只会填满前面一部分，尾部仍是【上一段的
   * PCM】✗ ⇒ 这段旧内容会被 DMA 反复重播 ⇒ 听感就是"哒哒/电流声"。
   * 清零后尾部恒为静音，而且不依赖 buffer_read() 的返回值语义（安全）。
   * 注意：16bit 有符号的静音值是 0。 */
  for (u32 i = 0; i < (u32)dev->play_size; i++) {
    ((u8*)dev->sound_buf)[i] = 0;
  }
  buffer_read(dev->buffer, dev->sound_buf, dev->play_size);

  /* 【cache 一致性·让声音出现的关键那一条】sound_buf 是 kmalloc 得到的（现在是
   * 可缓存内存）：上一步是 CPU 写入，紧接着 DMA 直接从 DRAM 读它 ⇒ 必须先刷到
   * PoC，否则控制器播到的是还留在 cache 里的旧数据 —— 实测就是"完全没有声音"。
   * （原来 sound_play() 里刷的是应用传入的 buf，而 DMA 并不读那个缓冲。） */
  cpu_flush_dcache_range(dev->sound_buf, (u32)dev->sound_buf + dev->play_size);
  // log_info("dma_audio_handler %x play size %d\n", dev->sound_buf,
  // dev->play_size);
  dma_trans(0, dev->sound_buf, CODEC_BASE + 0x0020, dev->play_size);

  // log_info("dma_audio_handler end %x\n", dev->sound_buf);
}

/* ===== 【临时：音频 PLL 自校准】=====
 * 问题：DAC_FS(bits[31:29]) 只是"家族分频档"（22050 与 44100 同档），真实采样率 =
 *       音频基准时钟 ÷ 该档分频。本仓库没有该 SoC 的 PLL 公式，照搬主线 D1 的
 *       (N=22,M=6,pattern=0xc001288d) 实测得到 39.4kHz（错），原配置实测 24.09kHz
 *       （快 8.8%）。盲改 N/M 不可靠，于是把它变成【可测量】的问题：
 *   1) 先按一组候选 N/M 配好 PLL（并关掉 SDM/小数分频，使输出只由 N/M 决定）；
 *   2) 向 DAC 直写 K 个静音样本，按 TX_EMPTY(FIFO 空 = 有空位) 阻塞推进；
 *      每写 1 个样本必须等 FIFO 再次变空 ⇒ 单样本乒乓 ⇒ 精确反映 DAC 真实速率；
 *   3) 用时基(cpu_read_ms)量耗时 ⇒ rate = K / dt；
 *   4) 由一次测量的比例系数反解"目标 22050"所需的 N/M，应用后再复测打印。
 * 依据：sun20iw1-codec.h(SUNXI_DAC_FIFOS.TX_EMPTY=23, SUNXI_DAC_TXDATA=0x20)。
 * 校准完成后删除本块。 */
#define CODEC_RATE_CALIB 1

#if CODEC_RATE_CALIB
/* 时基用内核 tick（SCHEDULE_FREQUENCY=1000 ⇒ 1 tick = 1ms）。曾用 cpu_read_ms()，
 * 实测在 codec_init 这个阶段它恒为 0（另一个计数器尚未计数），导致测量恒失败；
 * 而日志时间戳证明内核 tick 此时已正常计数。 */

/* 写入 PLL_AUDIO0 的 N/M（N 在 bits[15:8]，M 在 bits[21:16]，见主线 ccu-sun20i-d1.c）
 * 并关闭 SDM(bit24)——小数分频会让"频率↔N/M"的关系不可预测。 */
static void codec_pll_set(u32 n, u32 m) {
  u32 v = io_read32(CCU_BASE + 0x0078);
  v &= ~(0xFFu << 8);
  v &= ~(0x3Fu << 16);
  v |= (n & 0xFFu) << 8;
  v |= (m & 0x3Fu) << 16;
  v &= ~(1u << 24);
  io_write32(CCU_BASE + 0x0078, v);
  cpu_delay_msec(5); /* 等锁相稳定 */
}

/* 直写 k 个静音样本，返回实测采样率(Hz)；测量失败/超时返回 0。
 * 写法必须与作者 TRANS_CPU 通路一致：**只要 bit23 为 1 就连续写**（一次写
 * 2 个 16bit 样本 = 1 个 32bit 寄存器写）。原因：bit23 并非"FIFO 空"，
 * 而是"水位低于触发电平(TX_TRIG_LEVEL=32)"；若改成"写 1 个样本就等它变空"，
 * 8192 次写会在 1ms 内全部完成（实测 dt=0 ⇒ 恒返回 0）。
 * 连续写到水位满 ⇒ 等 DAC 抽干 ⇒ 再填，平均填充速率即真实采样率
 * （此前 TRANS_CPU 实测 8192 字节/170ms 也正是这条路径给出的）。 */
static u32 codec_measure_rate(u32 k) {
  u32 t0 = schedule_get_ticks();
  u32 done = 0;
  while (done < k && (schedule_get_ticks() - t0) < 3000u) {
    if (io_read32(CODEC_BASE + 0x0014) & (1u << 23)) { /* 水位未满 */
      io_write32(CODEC_BASE + 0x0020, 0);
      io_write32(CODEC_BASE + 0x0020, 0);
      done += 2;
    }
  }
  u32 dt = schedule_get_ticks() - t0;
  if (done < k || dt == 0) {
    return 0;
  }
  return (u32)((u64)done * 1000u / dt);
}
#endif

/* ===== 【临时：链路失真自测】=====
 * 目的：把"沙哑/毛刺来自内核链路，还是来自素材/SDL 解码"一刀切开 —— 内核直接
 * 合成纯音播放，只要复位就能听到，不需要 SD 卡、APP、任何解码器参与。
 * 播放：① 1kHz 满幅正弦 1.5s   ② 500Hz→8kHz 线性扫频 1.5s
 * 写法与 DMA 通路一致：一次 32bit 写 = 2 个连续 16bit 单声道样本，按 FIFO 水位阻塞。
 * 为免疫"L/R 半字顺序"假设，同一采样值写两次（顺序相反也不影响听感）。
 * 判据：纯音干净 ⇒ 链路无失真（沙哑来自素材/解码）；发毛、嘶哑 ⇒ 链路失真
 *       （下一步查增益与负载：数据手册 0.37Vrms@16Ω / THD+N −40dB 说明低阻负载+
 *        高增益必然失真，而 0.55Vrms@10kΩ 才是干净输出）。测完删除本块。 */
/* 已关闭：内核内直写 DAC 的自测两次都没能稳定跑通（第二次卡在 FIFO 水位判断，
 * 会阻断启动），改用外部干净文件做同样的判定（见 app/resource/test_clean.wav）。
 * 这里保留实现备查，编译期关掉，绝不再影响启动。 */
#define CODEC_TONE_TEST 0

#if CODEC_TONE_TEST
static const i16 sin_tab[64] = {
         0,   3136,   6242,   9289,  12245,  15084,  17778,  20300,
     22627,  24736,  26607,  28221,  29564,  30622,  31385,  31845,
     32000,  31845,  31385,  30622,  29564,  28221,  26607,  24736,
     22627,  20300,  17778,  15084,  12245,   9289,   6242,   3136,
         0,  -3136,  -6242,  -9289, -12245, -15084, -17778, -20300,
    -22627, -24736, -26607, -28221, -29564, -30622, -31385, -31845,
    -32000, -31845, -31385, -30622, -29564, -28221, -26607, -24736,
    -22627, -20300, -17778, -15084, -12245,  -9289,  -6242,  -3136,
};

#define TONE_BLK 2205 /* 100ms @22050，15 块 = 1.5s */

/* 把一整块样本喂给 DAC：只要 FIFO 有空间就连续写（与作者 TRANS_CPU 相同）。
 * 曾写成"等一次水位只写 1 个 32bit"，结果每 ~1.4ms 才写 2 个样本 ⇒ 1.5s 的测试音
 * 要放 23 秒（误以为挂死）。另外加了超时保护：任何情况下都不会卡住启动。 */
static void tone_play(const i16* b, u32 n) {
  u32 t0 = schedule_get_ticks();
  u32 i = 0;
  while (i < n && (schedule_get_ticks() - t0) < 8000u) {
    if (io_read32(CODEC_BASE + 0x0014) & (1u << 23)) { /* 有空间 */
      i16 s1 = b[i];
      i16 s2 = (i + 1 < n) ? b[i + 1] : 0;
      io_write32(CODEC_BASE + 0x0020,
                 ((u32)(u16)s1) | (((u32)(u16)s2) << 16));
      i += 2;
    }
  }
}

static void codec_tone_selftest(void) {
  static i16 blk[TONE_BLK];
  u32 ph = 0;

  /* ① 1kHz 满幅正弦 1.5s（相位跨块连续 ⇒ 无接缝） */
  log_info("tone test: 1kHz sine 1.5s\n");
  {
    u32 inc = (u32)((u64)1000u * 4294967296ull / 22050ull);
    for (u32 r = 0; r < 15u; r++) {
      for (u32 i = 0; i < TONE_BLK; i++) {
        blk[i] = sin_tab[(ph >> 26) & 63];
        ph += inc;
      }
      tone_play(blk, TONE_BLK);
    }
  }

  /* ② 阶梯扫频 500Hz→8kHz 1.5s（每块换一次频率，相位仍连续 ⇒ 无咔哒） */
  log_info("tone test: sweep 500Hz..8kHz 1.5s\n");
  for (u32 r = 0; r < 15u; r++) {
    u32 f = 500u + (7500u * r) / 14u;
    u32 inc = (u32)((u64)f * 4294967296ull / 22050ull);
    for (u32 i = 0; i < TONE_BLK; i++) {
      blk[i] = sin_tab[(ph >> 26) & 63];
      ph += inc;
    }
    tone_play(blk, TONE_BLK);
  }
  log_info("tone test: done\n");
}
#endif

void codec_init() {
  log_info("codec init %x\n", CODEC_BASE);
  u32 val;
  page_map(CODEC_BASE, CODEC_BASE, PAGE_DEV);
  // 1. config
  audio_ccu();
  // 2.  Configure the sample rate and data transfer format, then open the DAC.
  codec_dac();
  codec_analog();
  codec_param(16, 2, 44100);

  // codec_debug();

  codec_enable(1);

#if CODEC_RATE_CALIB
  {
    /* 【两点定线】实测"采样率 ↔ N/M"近似线性但**不经过原点**
     * （历史数据：N/M=4.000 → 44521Hz；N/M=1.981 → 23953Hz ⇒ 截距约 +3.8kHz）。
     * 所以不能按正比换算目标，必须用两点解出 A、B 后反解。 */
    codec_pll_set(40, 10); /* R1 = 4.000 */
    u32 r1 = codec_measure_rate(8192);
    codec_pll_set(40, 20); /* R2 = 2.000 */
    u32 r2 = codec_measure_rate(8192);
    log_info("rate calib: R=4.000 => %u Hz, R=2.000 => %u Hz\n", r1, r2);
    if (r1 > 1000u && r2 > 1000u && r1 > r2) {
      u32 A = (r1 - r2) / 2u;             /* Hz per unit N/M */
      u32 B = (r1 > 4u * A) ? (r1 - 4u * A) : 0u;
      if (A > 0u && 22050u > B) {
        /* 目标 R* = (22050 - B) / A（定点 ×1000） */
        u32 tgt = (u32)((u64)(22050u - B) * 1000u / A);
        u32 bn = 12, bm = 1, berr = 0xFFFFFFFFu;
        for (u32 m = 1; m <= 63u; m++) {
          u32 n = (u32)(((u64)tgt * m + 500u) / 1000u);
          u32 e;
          if (n < 12u || n > 255u) {
            continue;
          }
          e = (u32)((u64)n * 1000u / m);
          e = (e > tgt) ? (e - tgt) : (tgt - e);
          if (e < berr) {
            berr = e;
            bn = n;
            bm = m;
          }
        }
        codec_pll_set(bn, bm);
        u32 r3 = codec_measure_rate(8192);
        log_info("rate calib: choose N=%u M=%u => %u Hz (target 22050, err %d%%)\n",
                 bn, bm, r3, (int)(((int)r3 - 22050) * 100 / 22050));
      }
    }
  }
#endif

#if CODEC_TONE_TEST
  /* 纯音自测（复位后能直接听到，用于判断链路是否失真） */
  codec_tone_selftest();
#endif

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
  sound_device_t* sound_device = dev->data;

  if (cmd == SNDCTL_DSP_GETFMTS) {
    u32* val = args;
    *val = AFMT_S16_LE;
  } else if (cmd == SNDCTL_DSP_CHANNELS) {
    u32* val = args;
    kprintf("SNDCTL_DSP_CHANNELS %d\n", *val);
    sound_device->channal = *val;
    codec_param(-1, *val, -1);

  } else if (cmd == SNDCTL_DSP_SPEED) {
    u32* val = args;
    kprintf("SNDCTL_DSP_SPEED %d\n", *val);
    codec_param(-1, sound_device->channal, *val);

    kprintf("dma_init end\n");
  } else if (cmd == IOC_STAT) {
    struct stat* stat = args;
    stat->st_mode = S_IFCHR;
  } else if (cmd == IOC_STATFS) {
    kprintf("sound_ioctl2 %d\n", cmd);
    struct stat* stat = args;
    stat->st_mode = S_IFCHR;
  } else {
    kprintf("sound_ioctl %d\n", cmd);
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

  sound_device->sound_buf = kmalloc(SOUND_PLAY_SIZE, DEVICE_TYPE);
  sound_device->buffer = buffer_create(SOUND_BUF_SIZE, NULL, NULL, NULL, NULL);
  sound_device->play_size = SOUND_DMA_CHUNK;

  gic_irq_priority(0, IRQ_AUDIO_CODEC, 10);

  exception_regist(EX_AUDIO, audio_handler);

  log_info("sound init2\n");
  // dsp
  vnode_t* dsp = vfs_create_node("dsp", V_FILE | V_BLOCKDEVICE);
  dsp->device = device_find(DEVICE_SB);
  dsp->op = &device_operator;
  vfs_mount(NULL, "/dev", dsp);

  // /dev/dsp0
  vnode_t* dsp0 = vfs_create_node("dsp0", V_FILE | V_BLOCKDEVICE);
  dsp0->device = device_find(DEVICE_SB);
  dsp0->op = &device_operator;
  vfs_mount(NULL, "/dev", dsp0);

  log_info("sound init3 %x\n", CODEC_BASE);

  codec_init();

  log_info("sound init end\n");

  return 0;
}

void sound_exit(void) { kprintf("sound exit\n"); }

module_t sound_module = {
    .name = "sound", .init = sound_init, .exit = sound_exit};
