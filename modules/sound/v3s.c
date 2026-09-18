/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
/* ============================================================================
 * V3s 内置 Audio Codec 播放驱动（DMA 通路）
 *
 * 【硬件依据】《Allwinner_V3s_User_Manual_V1.0.pdf》4.13 章 + 4.3 章 CCU
 *   · Codec 数字部分基址 AC = 0x01c22c00（4.13.4 寄存器表）
 *       AC_DAC_DPC   0x000  EN_DAC=b31, HPF_EN=b18, DVOL=b17:12（0dB=0）
 *       AC_DAC_FIFOC 0x004  DAC_FS=b31:29（采样率家族）, FIR_VER=b28,
 *                           SEND_LASAT=b26（欠载发 0=静音）, FIFO_MODE=b25:24,
 *                           DAC_DRQ_CLR_CNT=b22:21, TX_TRIG_LEVEL=b14:8,
 *                           DAC_MONO_EN=b6, TX_SAMPLE_BITS=b5, DAC_DRQ_EN=b4,
 *                           DAC_IRQ_EN=b3, FIFO_UNDERRUN_IRQ_EN=b2,
 *                           FIFO_OVERRUN_IRQ_EN=b1, FIFO_FLUSH=b0
 *       AC_DAC_FIFOS 0x008  TX_EMPTY=b23, TXE_CNT=b22:8, TXE_INT=b3 / TXU_INT=b2
 *                           / TXO_INT=b1（都是写 1 清）
 *       AC_DAC_TXDATA 0x020 ← 样本写这里（**不是 0x0C**，见 v3s-reg.h 的订正）
 *       AC_DAC_CNT   0x040  写 0 清零
 *   · 模拟部分【不在 AC 基址上】：经 AC_PR_CFG（0x01c23000）的 8 位索引窗口访问
 *       b28=AC_PR_RST(1=de-assert)  b24=AC_PR_RW(1=写/0=读)
 *       b20:16=Addr  b15:8=写数据  b7:0=读数据
 *     用到：0x00 耳机音量、0x01/0x02 左右输出混音、0x03 DAC/PA 使能与源、
 *           0x07 HP_CTRL（HPPAEN=b7, HPCOM_FC=b6:5, COMPTEN=b4, COS_SLOPE=b3:2）
 *   · 时钟
 *       PLL_AUDIO = 24MHz*N/(M*P)（CCU 0x008）：N=b14:8、M=b4:0、P=b19:16，
 *         三者都写"因子"（实际值 = 写值+1）；PLL_ENABLE=b31、LOCK=b28、SDM_EN=b24
 *         （本驱动不用小数分频，保证"频率↔N/M"可预测）。
 *       SCLK = PLL_AUDIO（AC_DIG_CLK 的 b31 门控），DAC 内部按 DAC_FS 分频：
 *         48k 家族 SCLK = 512×fs；44.1k/22.05k/11.025k 家族要把 PLL 挪到 22.5792MHz
 *         （手册 4.13.5.2 的 DAC_FS 位表明确写了这一条）。
 *       ⇒ 44.1k 家族：N=32,M=17,P=2 → 22.5882MHz（+0.04%，听不出来）
 *          48k  家族：N=33,M=32,P=1 → 24.7500MHz（+0.7%）
 *       门控/复位：BUS_CLK_GATE2(0x068) b0=AC_DIG_GATING；
 *                 BUS_SOFT_RST3(0x2D0) b0=AC_DIG_RST（1=de-assert）。
 *   · DMA：DRQ 端口 DMAC_CFG_TYPE_AUDIO（sunxi-dma.h 的 V3S 分支 = 15），
 *       源 DRAM → 目的 AC_DAC_TXDATA。**通道避开 LCD**（st7789 用 0；两者共用
 *       sunxi-dma.c 的回调槽，共用通道会互相覆盖），本驱动用通道 1。
 *
 * 【与 t113-s3.c 的差异·别照抄】
 *   1) 偏移不同：t113 的 DAC_FIFOC 在 0x10 / TXDATA 在 0x20；V3s 的 DAC_FIFOC 在
 *      0x04 / TXDATA 在 0x20；
 *   2) V3s 的模拟部分要过 PR_CFG 窗口，不是直接 MMIO；
 *   3) V3s 的 PLL_AUDIO 有独立 P（post-div），公式 24*N/(M*P)。
 *   4) V3s 的 DMA 是另一代引擎：DMA_CFG_REG 位布局、DMA_PARA_REG 的 DATA_BLK_SIZE
 *      都与 t113 不同（见 duck/modules/dma/sunxi-dma.c 里 #if defined(V3S) 的两处）。
 *
 * 【真机打通记录：这条链上一共踩了 7 个坑，前 6 个在数字侧、第 7 个在模拟侧】
 *   ① DMA mode 少 bit16（sound/v3s.c）
 *      现象：环形缓冲塞满 64KB 后音频彻底停摆、完成中断永不触发。
 *      根因：dma_set_mode() 的 bit16 = "流式 DRQ 传输"，缺了它 ⇒ loop_mode=1（LLI 的
 *            link 指向自己）+ commit_para=0x0200（DRQ 等待位为 0）。
 *   ② V3s 的 dma_reg_t 少占位字（dma/sunxi-dma.h）
 *      现象：修完 ① 后 DMA 依然像"死的"（一个样本都不搬）。
 *      根因：结构体 channel[] 落在 0xF8 而不是手册的 0x100 ⇒ 通道寄存器整体错位
 *            8 字节 ⇒ desc_addr/enable 全写进保留区。dma_init_all() 现在有一行
 *            "dma: base ... channel0 ... (expect ...)" 专门盯这个。
 *   ③ gic_irqack() 写不存在的 GICC_DIR（libarchcommon/arm/gic2.c）
 *      现象：DMA 完成中断第一次真正到来时 "exception inside IRQ handler"，pc 停在
 *            gic_irqack 的 DIR 那条 str（目标 0x01c83000，未映射）。
 *      根因：通用实现按 GIC-400 规范写 GICC+0x1000；V3s 的 GICC 窗口只有 4KB，
 *            平台自己的 gic_irqack2() 也只写 EOIR。⇒ V3s 下跳过 DIR。
 *   ④ DMA_PARA_REG 的 DATA_BLK_SIZE 必须是 burst×width 的倍数（dma/sunxi-dma.c）
 *      现象：每秒完成 11324 次、每次 FIFO 只多 0~1 个字（46MB/s 碎片传输）。
 *      根因：t113 那边把 N 写 0 可行，V3s 上 N=0 ⇒ 每次 DRQ 只搬一个 burst。
 *            ⇒ #if defined(V3S) 取 64 字节。
 *   ⑤ DMA_CFG_REG 位布局与 t113 不同（dma/sunxi-dma.c）
 *      现象：每次 4096 字节仅 ~88µs 就"完成"，DAC 拿不到连续数据。
 *      根因：共享的 dma_channel_config_t 把 burst 放 [6:7]/[22:23]、addr_mode 放
 *            [8]/[24]；V3s 手册是 addr_mode [6:5]/[22:21]、burst [8:7]/[24:23]
 *            ⇒ 目的地址模式落到保留值 2【不是 IO 模式】⇒ DMA 以线性地址往 codec
 *            寄存器区连写 4096 字节（只有第一个字落进 TXDATA）。
 *            ⇒ 按 V3s 手册重建配置字（修复后 cfg=52f0501：dst drq=15 am=1 w=2）。
 *   ⑥ 挂起位只清 PKG、漏掉 HALF/QUEUE（dma/sunxi-dma.c）
 *      现象：修复 ⑤ 后中断仍以 11324 次/秒风暴式重复投递。
 *      根因：DMA_IRQ_PEND_REG 每通道 4 bit（HALF/PKG/QUEUE，写 1 清），老代码只清
 *            PKG ⇒ 另两位常置位 ⇒ 中断线一直有效。⇒ 0x7 掩码一次清完。
 *   ⑦ PR 窗口的写是"WRITE 位上升沿"触发的（本文件 ac_pr_write）
 *      现象：数字侧逐项实证正确（CNT 实测 44101Hz、FIFO 稳定在触发水位、DMA 被
 *            DRQ 节流），但耳机里**一点声音都没有**。
 *      根因：模拟寄存器经 AC_PR_CFG(0x01c23000) 访问，mainline(sun8i-adda-pr-regmap.c)
 *            的写时序是"设地址 → 设数据 → WRITE=1 → WRITE=0"；老实现把 WRITE 一直
 *            留在 1 ⇒ 只有开机后第一次写生效，之后 HPV/DAC_PA_SRC/混音/耳放**全部
 *            被静默丢弃** ⇒ 数字通路完美、模拟全关。
 *  （另有 V3s 平台补的 interrupt_last_irq/interrupt_ack_pending()，与 t113 对齐的
 *    中断兜底；见 duck/platform/v3s/init.c。）
 * ========================================================================== */

#include "dev/devfs.h"
#include "dma/dma.h"
#include "dma/sunxi-dma.h" /* DMAC_CFG_TYPE_* */
#include "gpio.h"          /* V3s 平台头：含 v3s-ccu.h(CCU_BASE) */
#include "kernel/kernel.h"
#include "sound.h"

#include "archcommon/io.h" /* io_read32/io_write32 */

void dma_audio_handler(void* data); /* DMA 完成回调（下面实现） */

/* ---------------- Codec 数字寄存器（基址 0x01c22c00） ---------------- */
#define AC_BASE 0x01C22C00
#define AC_DAC_DPC 0x000
#define AC_DAC_FIFOC 0x004
#define AC_DAC_FIFOS 0x008
#define AC_DAC_TXDATA 0x020
#define AC_DAC_CNT 0x040
#define AC_DAC_DG 0x048      /* DAC Debug：b8 CODEC_CLK_SELECT(0=PLL/1=OSC)、
                              * b11 DAC_MODU_SELECT、b10:9 DAC_PATTERN_SELECT */
#define AC_DAC_DAP_CTR 0x060 /* b31 DDAP_EN(0=bypass/1=enable)、b15 DRC、b14 HPF */
#define AC_ADC_FIFOC 0x010   /* b28 EN_AD：ADC 数字部分使能 */
#define AC_ADC_FIFOS 0x014   /* b23 RXA、b13:8 RXA_CNT（可用样本数） */

#define DAC_DPC_EN_DAC (1u << 31)
#define DAC_DPC_HPF_EN (1u << 18)

#define DAC_FIFOC_FS_SHIFT 29
#define DAC_FIFOC_FIFO_MODE_SHIFT 24
#define DAC_FIFOC_TX_TRIG_SHIFT 8
#define DAC_FIFOC_MONO_EN (1u << 6)
#define DAC_FIFOC_SAMPLE_BITS_20 (1u << 5)
#define DAC_FIFOC_DRQ_EN (1u << 4)
#define DAC_FIFOC_FLUSH (1u << 0)

#define DAC_FIFOS_ERR_BITS ((1u << 3) | (1u << 2) | (1u << 1))

/* ---------------- 模拟寄存器访问窗口（0x01c23000） ---------------- */
#define AC_PR_BASE 0x01C23000
#define AC_PR_RST (1u << 28)
#define AC_PR_WR (1u << 24)
#define AC_PR_ADDR_SHIFT 16
#define AC_PR_WDATA_SHIFT 8

#define AC_PAG_HPV 0x00 /* 耳机音量：0=mute，0x3f 最大 */
#define AC_LOMIXSC 0x01 /* 左输出混音：b1=左DAC b0=右DAC */
#define AC_ROMIXSC 0x02 /* 右输出混音 */
#define DAC_PA_SRC 0x03 /* DAC/PA 使能与源选择 */
#define HP_CTRL 0x07    /* PA 使能与 HP 控制 */

/* 【PR 窗口写·必须按主线的时序 —— 这是"数字侧全对却完全没声音"的根因】
 * 参照 linux-6.3.1/sound/soc/sunxi/sun8i-adda-pr-regmap.c 的 adda_reg_write()：
 *   1) 确保 AC_PR_RST(b28)=1（解除复位）
 *   2) 清 AC_PR_WR(b24)=0
 *   3) 写地址 AC_PR_ADDR(b20:16)
 *   4) 写数据 AC_PR_WDATA(b15:8)
 *   5) 置 AC_PR_WR=1 ——【写是用 WRITE 位的上升沿触发的】
 *   6) 再清 AC_PR_WR=0
 * 老实现把这三件事拼成一次写、并把 WRITE 一直留在 1 上：
 *     io_write32(AC_PR_BASE, RST | WR | addr | val);
 * ⇒ 只有上电后第一次写（0→1）真正生效，**之后所有模拟寄存器写全被硬件忽略**
 *   ⇒ 耳机音量(PAG_HPV)、DAC/混音使能(DAC_PA_SRC)、左右混音源(LOMIXSC/ROMIXSC)、
 *     耳放使能(PAEN_HP_CTRL)……其实都没写进去 ⇒ 数字通路完全正常(实测 44101Hz)
 *     但耳朵里一点声音都没有。 */
static void ac_pr_write(u8 addr, u8 val) {
  u32 v;

  io_write32(AC_PR_BASE, io_read32(AC_PR_BASE) | AC_PR_RST); /* 解除复位 */

  v = io_read32(AC_PR_BASE) & ~AC_PR_WR; /* 清 WRITE */
  io_write32(AC_PR_BASE, v);

  v &= ~(((u32)0x1f) << AC_PR_ADDR_SHIFT);
  v |= ((u32)addr & 0x1f) << AC_PR_ADDR_SHIFT;
  io_write32(AC_PR_BASE, v); /* 地址 */

  v &= ~(((u32)0xff) << AC_PR_WDATA_SHIFT);
  v |= ((u32)val & 0xff) << AC_PR_WDATA_SHIFT;
  io_write32(AC_PR_BASE, v); /* 数据 */

  io_write32(AC_PR_BASE, io_read32(AC_PR_BASE) | AC_PR_WR);  /* WRITE=1 */
  io_write32(AC_PR_BASE, io_read32(AC_PR_BASE) & ~AC_PR_WR); /* WRITE=0 ← 触发写 */
}

/* 读：同样按主线的 adda_reg_read()：RST=1 → WRITE=0 → 设地址 → 读 b7:0 */
static u8 ac_pr_read(u8 addr) {
  u32 v;

  io_write32(AC_PR_BASE, io_read32(AC_PR_BASE) | AC_PR_RST);
  io_write32(AC_PR_BASE, io_read32(AC_PR_BASE) & ~AC_PR_WR);

  v = io_read32(AC_PR_BASE);
  v &= ~(((u32)0x1f) << AC_PR_ADDR_SHIFT);
  v |= ((u32)addr & 0x1f) << AC_PR_ADDR_SHIFT;
  io_write32(AC_PR_BASE, v);

  return (u8)(io_read32(AC_PR_BASE) & 0xff);
}

/* ---------------- 时钟/寄存器常量 ---------------- */
/* CCU 寄存器名沿用平台头 v3s-ccu.h：CCU_PLL_AUDIO_CTRL / CCU_BUS_CLK_GATE2 /
 * CCU_AC_DIG_CLK / CCU_BUS_SOFT_RST3（别再自己重复定义，会撞名字）。 */
#define PLL_AUDIO_PLL_ENABLE (1u << 31)
#define PLL_AUDIO_LOCK (1u << 28)
#define PLL_AUDIO_SDM_EN (1u << 24)
#define PLL_AUDIO_N_SHIFT 8  /* 写 N-1 */
#define PLL_AUDIO_M_SHIFT 0  /* 写 M-1 */
#define PLL_AUDIO_P_SHIFT 16 /* 写 P-1 */
#define AC_DIG_CLK_1X_GATING (1u << 31)
#define AC_DIG_GATING (1u << 0)
#define AC_DIG_RST (1u << 0)

/* 采样率 → DAC_FS 家族位 + 是否 44.1k 家族（决定 PLL 档位） */
typedef struct {
  u32 rate;
  u32 fs;
  u32 fam441;
} v3s_rate_t;

static const v3s_rate_t v3s_rate_tab[] = {
    {8000, 5, 0},  {11025, 4, 1}, {12000, 4, 0}, {16000, 3, 0},
    {22050, 2, 1}, {24000, 2, 0}, {32000, 1, 0}, {44100, 0, 1},
    {48000, 0, 0},
};

/* 当前 PLL 家族（1 = 44.1k 档 22.5882MHz，0 = 48k 档 24.75MHz）。
 * 不能由 DAC_FS 反推：FS 只是分频档，8000(fs=5)/11025(fs=4)/12000(fs=4) 会撞车。 */
static u32 g_fam441 = 1;

/* 调试开关：1 = 中断里前 6 次打印 ring/FIFO 状态（做 bring-up 时用） */
#define AUDIO_DBG 0

/* 音频 DMA 通道：st7789(LCD) 占通道 0，音频用 1 隔离 */
#define SOUND_DMA_CHANNEL 1
#define SOUND_DMA_CHUNK 4096 /* ≈23ms@44.1k/16bit/stereo */
#define SOUND_BUF_SIZE (64 * 1024)
#define SOUND_BUF_ALLOC 8192

/* ---------------- 【中断安全的内存：一律放 .bss】 ----------------
 * duck/kernel/memory.c 的 kmalloc()：
 *     flag & (KERNEL_TYPE|DEVICE_TYPE) ⇒ phy_alloc()  —— 物理直映射，任何地址空间可见
 *     否则                             ⇒ vm_alloc()   —— 0x9xxxxxxx，只在【当前进程】页表里
 * 而 DMA 完成中断会在【任意线程上下文】里执行（实测：tone 测试期间中断落在 tid:1）：
 * 一旦回调里解引用 vm_alloc 出来的内存，就是 data abort ——
 *   "exception inside IRQ handler no=2, pc=<dma_audio_handler 的第一条 dev->sound_buf>"
 * （内核自己也在 kmalloc() 的 ARMv5 分支写过同一件事：kernel thread 用 vm_alloc 分配的
 *  内存在用户页表里不存在，会 fault。）
 * ⇒ 所以音频的 环形缓冲 / DMA 搬运缓冲 / 设备结构体 全部用静态内存：.bss 属于内核镜像，
 *   每个地址空间都映射，不用管当前是谁在跑；顺带省掉堆分配。 */
static u8 g_ring[SOUND_BUF_SIZE];        /* 环形缓冲（应用写 / DMA 读） */
static u32 g_ring_r, g_ring_w, g_ring_n; /* 读指针 / 写指针 / 积压字节数 */
static u8 g_dma_buf[SOUND_BUF_ALLOC]
    __attribute__((aligned(64))); /* DMA 源缓冲（cache 行对齐） */
static sound_device_t g_snd_dev;  /* 设备实例 */
/* DMA 完成回调计数：每次回调 = 搬完 play_size(4096) 字节 = 1024 帧。
 * 用"1 秒内的回调次数 × 1024"反推【真实采样率】，用来核对 PLL/SCLK 与 DAC_FS 档位
 * 是否匹配（例如 SCLK 误取到 4X 档，就会正好快 4 倍 ⇒ 声音又尖又快）。 */
static volatile u32 g_dma_irq_count;

/* 静态环形缓冲的 4 个操作，替换原来的 buffer_*（原因见上） */
static size_t ring_size(void) { return g_ring_n; }

static void ring_clear(void) { g_ring_r = g_ring_w = g_ring_n = 0; }

static size_t ring_write(const u8* src, size_t len) {
  if (len > SOUND_BUF_SIZE) { /* 一次写入超过整圈：只留最后一段 */
    src += len - SOUND_BUF_SIZE;
    len = SOUND_BUF_SIZE;
  }
  if (len > SOUND_BUF_SIZE - g_ring_n) { /* 放不下就丢最旧的（调用方会先 ring_clear） */
    u32 drop = (u32)(len - (SOUND_BUF_SIZE - g_ring_n));
    g_ring_r = (g_ring_r + drop) % SOUND_BUF_SIZE;
    g_ring_n -= drop;
  }
  for (size_t i = 0; i < len; i++) {
    g_ring[g_ring_w] = src[i];
    g_ring_w = (g_ring_w + 1) % SOUND_BUF_SIZE;
  }
  g_ring_n += len;
  return len;
}

/* 数据不足时只填前半截，剩下的由调用方负责（先清零 ⇒ 尾部恒为静音） */
static size_t ring_read(u8* dst, size_t len) {
  size_t n = len < g_ring_n ? len : (size_t)g_ring_n;
  for (size_t i = 0; i < n; i++) {
    dst[i] = g_ring[g_ring_r];
    g_ring_r = (g_ring_r + 1) % SOUND_BUF_SIZE;
  }
  g_ring_n -= n;
  return n;
}

/* 中断守卫用的合法设备指针（防别的 DMA 通道误调用本回调） */
static sound_device_t* g_sound_dev = NULL;
/* 设备实例（供 sysconf 回调里的自测音使用；g_sound_dev 只在开播后才非空） */
static sound_device_t* g_snd = NULL;

/* 毫秒延时：用内核 tick（1tick=1ms@1kHz），避免引入未声明的 cpu_delay_msec */
static void codec_delay_ms(u32 ms) {
  u32 t0 = schedule_get_ticks();
  while ((schedule_get_ticks() - t0) < ms) {
  }
}

/* ---------------- 采样率 / PLL ---------------- */

static void codec_pll_set(u32 fam441) {
  u32 n = fam441 ? 32u : 33u;
  u32 m = fam441 ? 17u : 32u;
  u32 p = fam441 ? 2u : 1u;
  u32 val;

  val = io_read32(CCU_BASE + CCU_PLL_AUDIO_CTRL);
  val &= ~(0xffu << PLL_AUDIO_N_SHIFT);
  val &= ~(0x1fu << PLL_AUDIO_M_SHIFT);
  val &= ~(0xfu << PLL_AUDIO_P_SHIFT);
  val |= ((n - 1) & 0xffu) << PLL_AUDIO_N_SHIFT;
  val |= ((m - 1) & 0x1fu) << PLL_AUDIO_M_SHIFT;
  val |= ((p - 1) & 0xfu) << PLL_AUDIO_P_SHIFT;
  val &= ~PLL_AUDIO_SDM_EN;
  val |= PLL_AUDIO_PLL_ENABLE;
  io_write32(CCU_BASE + CCU_PLL_AUDIO_CTRL, val);

  /* 等锁相；带超时，绝不在启动路径上卡死 */
  for (int i = 0; i < 200000; i++) {
    if (io_read32(CCU_BASE + CCU_PLL_AUDIO_CTRL) & PLL_AUDIO_LOCK) {
      return;
    }
  }
  log_warn("v3s codec: PLL_AUDIO not locked (N=%d M=%d P=%d)\n", n, m, p);
}

/* 应用侧 SNDCTL_DSP_SET{FMT,CHANNELS,SPEED} 最终落到这里 */
static void codec_param(int format, int channal, int freq) {
  u32 fifoc = io_read32(AC_BASE + AC_DAC_FIFOC);
  u32 fs = (fifoc >> DAC_FIFOC_FS_SHIFT) & 0x7;

  if (freq > 0) {
    for (u32 i = 0; i < sizeof(v3s_rate_tab) / sizeof(v3s_rate_tab[0]); i++) {
      if (freq >= (int)v3s_rate_tab[i].rate) {
        fs = v3s_rate_tab[i].fs;
        g_fam441 = v3s_rate_tab[i].fam441;
      }
    }
  }
  /* freq <= 0：只改位宽/声道，沿用当前 FS 与家族 */
  codec_pll_set(g_fam441);

  fifoc &= ~(0x7u << DAC_FIFOC_FS_SHIFT);
  fifoc |= (fs & 0x7u) << DAC_FIFOC_FS_SHIFT;

  /* 位宽：本驱动只走 16bit（应用写的是 S16_LE）；20/24bit 时置 TX_SAMPLE_BITS */
  if (format == 24 || format == 20) {
    fifoc |= DAC_FIFOC_SAMPLE_BITS_20;
  } else {
    fifoc &= ~DAC_FIFOC_SAMPLE_BITS_20;
  }

  /* 声道：1 = mono（FIFO 128 级），其它按 stereo */
  if (channal == 1) {
    fifoc |= DAC_FIFOC_MONO_EN;
  } else {
    fifoc &= ~DAC_FIFOC_MONO_EN;
  }

  /* FIFO_MODE=11：每个 TXDATA 字取低 16bit（{TXDATA[15:0], 8'b0}）。
   * 16bit 小端样本按字节流过来时，正好被逐样本取用；
   * 20/24bit 用 00（取高 16bit），与手册位表一致。 */
  fifoc &= ~(0x3u << DAC_FIFOC_FIFO_MODE_SHIFT);
  fifoc |= ((format == 24 || format == 20) ? 0u : 3u)
           << DAC_FIFOC_FIFO_MODE_SHIFT;

  /* TX_TRIG_LEVEL=32：64 级 FIFO 降到 32 就发 DRQ，留足提前量 */
  fifoc &= ~(0x7fu << DAC_FIFOC_TX_TRIG_SHIFT);
  fifoc |= (32u << DAC_FIFOC_TX_TRIG_SHIFT);

  /* 打开 DMA 请求（本驱动靠 DRQ 搬运；FIFO 中断不需要） */
  fifoc |= DAC_FIFOC_DRQ_EN;

  io_write32(AC_BASE + AC_DAC_FIFOC, fifoc);
}

/* ---------------- 初始化 ---------------- */

static void codec_ccu_init(void) {
  u32 val;

  /* 1) 放通 AC_DIG 总线时钟 + 解除复位（否则后面写 codec 寄存器全部无效） */
  val = io_read32(CCU_BASE + CCU_BUS_CLK_GATE2);
  io_write32(CCU_BASE + CCU_BUS_CLK_GATE2, val | AC_DIG_GATING);

  val = io_read32(CCU_BASE + CCU_BUS_SOFT_RST3);
  io_write32(CCU_BASE + CCU_BUS_SOFT_RST3, val | AC_DIG_RST);

  /* 2) PLL_AUDIO 先按 44.1k 家族起振 */
  codec_pll_set(1);

  /* 3) 开出 SCLK = PLL_AUDIO（AC_DIG_CLK b31） */
  val = io_read32(CCU_BASE + CCU_AC_DIG_CLK);
  io_write32(CCU_BASE + CCU_AC_DIG_CLK, val | AC_DIG_CLK_1X_GATING);

  codec_delay_ms(10);
}

static void codec_dac_init(void) {
  u32 val;

  /* DAC 数字部分：EN_DAC=1、HPF_EN=1、DVOL=0（0dB） */
  val = io_read32(AC_BASE + AC_DAC_DPC);
  val |= DAC_DPC_EN_DAC;
  val |= DAC_DPC_HPF_EN;
  val &= ~(0x3fu << 12);
  io_write32(AC_BASE + AC_DAC_DPC, val);

  /* 复位 DAC FIFO（写 1 自清），并把触发水位/位宽/声道清成已知状态 */
  val = io_read32(AC_BASE + AC_DAC_FIFOC);
  val &= ~(0x7u << DAC_FIFOC_FS_SHIFT);
  val &= ~DAC_FIFOC_SAMPLE_BITS_20;
  val &= ~DAC_FIFOC_MONO_EN;
  val |= 32u << DAC_FIFOC_TX_TRIG_SHIFT;
  val |= DAC_FIFOC_FLUSH;
  io_write32(AC_BASE + AC_DAC_FIFOC, val);

  io_write32(AC_BASE + AC_DAC_CNT, 0);

  /* 采样率/位宽/声道 + 打开 DRQ（默认 44.1k/16bit/stereo，与 infones 一致） */
  codec_param(16, 2, 44100);
}

static void codec_analog_init(void) {
  /* 先经 PR 窗口 de-assert 复位（每次写都带 b28=1） */
  ac_pr_write(DAC_PA_SRC, 0x00);
  codec_delay_ms(2);

  /* DAC_PA_SRC 0x03（手册 4.13.5.125）
   *   b7 DACAREN 右 DAC 使能, b6 DACALEN 左 DAC 使能,
   *   b5 RMIXEN 右混音使能, b4 LMIXEN 左混音使能,
   *   b3 RHPPAMUTE=1 not mute, b2 LHPPAMUTE=1 not mute,
   *   b1/b0 RHPIS/LHPIS=0 → PA 输入取内部 DAC */
  ac_pr_write(DAC_PA_SRC,
              (1 << 7) | (1 << 6) | (1 << 5) | (1 << 4) | (1 << 3) | (1 << 2));

  /* 左右输出混音：b1=左 DAC、b0=右 DAC，都不静音 */
  ac_pr_write(AC_LOMIXSC, 0x03);
  ac_pr_write(AC_ROMIXSC, 0x03);

  /* 耳机音量（0=mute，0x3f 最大） */
  ac_pr_write(AC_PAG_HPV, 0x3f);

  /* HP_CTRL 0x07（手册 4.13.5.127）
   *   b7 HPPAEN=1 开耳机功放；b6:5 HPCOM_FC=11 直推 HPL/HPR；
   *   b4 COMPTEN=1 保护；b3:2 COS_SLOPE=01 防爆音 262ms；
   *   b1/b0 反相输出路径保持静音（只用正常路径） */
  ac_pr_write(HP_CTRL, (1 << 7) | (3 << 5) | (1 << 4) | (1 << 2));

  log_info("v3s codec analog: HPV=%02x PA_SRC=%02x HP=%02x\n",
           ac_pr_read(AC_PAG_HPV), ac_pr_read(DAC_PA_SRC), ac_pr_read(HP_CTRL));
}

/* ---------------- 【配置驱动的自测音】 ----------------
 * /conf/system.conf:
 *   [sound]
 *   test_tone = 1        # 启动时播 1kHz 正弦 1.5s（默认关）
 *
 * 【为什么要这个】验证"codec + DMA + 模拟通路"是否真的出声，用内核自己合成的
 * 已知信号最干净：不依赖卡上是否有播放器、不依赖游戏/模拟器是否产生 PCM
 * （实测 infones 的 APU 在标题/静止画面会长期输出 sum=0 的全零 PCM）。
 * 播放时机放在 sysconf 就绪回调里（fatfs 已挂、用户态还没起），此时 DMA/中断
 * 都已可用；只在配置打开时才执行，失败也不影响启动。
 * 依据：手册 AC_DAC_TXDATA(0x020) 写样本、TXE_CNT/TX_EMPTY(AC_DAC_FIFOS b22:8/b23)。 */
#ifdef SYSCONF_MODULE
#include "modules/sysconf/sysconf.h" /* 与 gpu/v3s.c 同一写法（include dir 有 '../../'） */

/* 正弦表：64 点，幅值 32000（-0.3dB 左右，留点余量避免削顶） */
static const i16 v3s_sin_tab[64] = {
    0,     3136,  6242,  9289,  12245, 15084, 17778, 20300, 22627, 24736,
    26607, 28221, 29564, 30622, 31385, 31845, 32000, 31845, 31385, 30622,
    29564, 28221,  26607, 24736, 22627, 20300, 17778, 15084, 12245, 9289,
    6242,  3136,   0,    -3136, -6242, -9289, -12245, -15084, -17778, -20300,
    -22627, -24736, -26607, -28221, -29564, -30622, -31385, -31845, -32000,
    -31845, -31385, -30622, -29564, -28221, -26607, -24736, -22627, -20300,
    -17778, -15084, -12245, -9289, -6242,  -3136};

static void sound_tone_play(sound_device_t* dev, u32 freq, u32 ms) {
  /* 立体声 16bit 小端：每帧 4 字节 (L,R)；一次填 1 个 chunk（4096B = 1024 帧） */
  static i16 frame[SOUND_DMA_CHUNK / 2];
  u32 ph = 0;
  u32 inc = (u32)(((u64)freq << 32) / 44100u); /* 相位步进（定点） */
  u32 total_frames = (44100u * ms) / 1000u;
  u32 done = 0;
  u32 t0 = schedule_get_ticks();

  log_info("v3s codec: tone test %dHz %dms\n", freq, ms);
  while (done < total_frames) {
    u32 n = SOUND_DMA_CHUNK / 4; /* 本次填多少帧 */
    if (n > total_frames - done) {
      n = total_frames - done;
    }
    for (u32 i = 0; i < n; i++) {
      i16 s = v3s_sin_tab[(ph >> 26) & 63];
      frame[i * 2] = s;
      frame[i * 2 + 1] = s;
      ph += inc;
    }
    sound_play(dev, frame, n * 4);
    done += n;
    /* 环形缓冲满了就等它排空（DMA 在别处推进；带超时，绝不卡死启动） */
    while (ring_size() > (SOUND_BUF_SIZE - SOUND_DMA_CHUNK)) {
      if ((schedule_get_ticks() - t0) > ms + 3000u) {
        log_warn("v3s codec: tone test timeout (queued %d frames, ring=%d CNT=%d "
                 "FIFOS=%x)\n",
                 (int)done, (int)ring_size(),
                 (int)io_read32(AC_BASE + AC_DAC_CNT),
                 io_read32(AC_BASE + AC_DAC_FIFOS));
        return;
      }
      /* 【硬证据】每 ~400ms 打一次"DMA 到底有没有在搬"：
       *   ring = 环形缓冲里积压的字节（DMA 在取数就该往下掉）
       *   CNT  = AC_DAC_CNT，DMA/CPU 每往 TX FIFO 写一个样本就 +1（手册 4.13.5.9）
       * CNT 不涨 ⇒ DMA 一个样本都没送进 FIFO；ring 不掉 ⇒ 回调没在续投。 */
      {
        static u32 dbg_last;
        u32 now = schedule_get_ticks();
        if (now - dbg_last > 400u) {
          dbg_last = now;
          /* 用 kprintf 直写串口：log_info 走 log 线程，一旦那个线程出事就"沉默了" */
          kprintf("tone wait ring=%d CNT=%d FIFOS=%x done=%d\n", (int)ring_size(),
                  (int)io_read32(AC_BASE + AC_DAC_CNT),
                  io_read32(AC_BASE + AC_DAC_FIFOS), (int)done);
        }
      }
      codec_delay_ms(5);
    }
  }
  /* 让最后一段播完再继续启动流程 */
  codec_delay_ms(400);
  kprintf("tone test done (CNT=%d)\n", (int)io_read32(AC_BASE + AC_DAC_CNT));
  log_info("v3s codec: tone test done\n");
}

/* 【codec 活性自检】判据（不依赖 DMA，纯 CPU 直写）：
 *   AC_DAC_FIFOS.TXE_CNT(b22:8) = "TX FIFO Empty Space Word Counter"（手册 4.13.5.3，
 *     复位默认 0x80=128）；AC_DAC_CNT(0x040) = "TX Sample Counter"（每送入一个样本 +1）。
 *   把 TX FIFO 填到没有空位 → 等 100ms → space 回到满值 ⇒ DAC 在抽数。
 *   【不要用 b23 TX_EMPTY 判空位】手册写 "1: 还有 ≥1 个空位"，实测写 1 个字它就翻 0
 *   （更像"FIFO 是否为空"），而且该状态跨时钟域刷新慢 ⇒ 会误判成"不抽数"。
 *
 * 真机实测（V3s）：DPC=80040000(EN_DAC+HPF_EN)、FIFOC=3002010(FIFO_MODE=11/TRIG=32/
 * DRQ_EN)、PLL_AUDIO=90011f10(N=32 M=17 P=2 → 22.5882MHz, LOCK=1)、GATE2=21
 * (AC_DIG_GATING)、RST3=1、AC_DIG_CLK=80000000(SCLK=PLL_AUDIO) ⇒ 填 130 字后 100ms 内
 * 被全部抽空。曾逐个试过的 DDAP_EN / clk=OSC24M / PLL 门控位 / PLL 重起振 / IRQ_EN /
 * MONO_EN / FIFO_MODE=00 / SEND_LASAT / 复位脉冲 表现完全一致（唯一"冻住"的是
 * DAC 自出波形那一步：DAC 不看 FIFO，本来就该冻住）⇒ codec 侧没有问题。 */
static u32 fifo_space(void) {
  return (io_read32(AC_BASE + AC_DAC_FIFOS) >> 8) & 0x7fff;
}

/* 填到"没有空位"为止（按 b22:8 计数器），最多 max_words 个 */
static u32 fifo_fill(u32 max_words) {
  u32 w = 0;
  while (w < max_words && fifo_space() > 0) {
    io_write32(AC_BASE + AC_DAC_TXDATA, 0);
    w++;
  }
  return w;
}

/* 一轮：填满 → 100ms → 看 space 是否回升 / CNT 是否增长 */
static void probe_step(const char* what) {
  u32 s0, s1, c0, c1, wr;

  wr = fifo_fill(200);
  s0 = fifo_space();
  c0 = io_read32(AC_BASE + AC_DAC_CNT);
  codec_delay_ms(100);
  s1 = fifo_space();
  c1 = io_read32(AC_BASE + AC_DAC_CNT);
  log_info("v3s probe [%s]: fill=%d space %d->%d CNT %d->%d %s\n", what, (int)wr,
           (int)s0, (int)s1, (int)c0, (int)c1,
           (s1 > s0 || c1 != c0) ? "==> DRAIN!" : "(frozen)");
}

static void codec_probe(void) {
  u32 v;

  codec_delay_ms(20);
  log_info("v3s probe: DG=%x DAP=%x FIFOS=%x CNT=%x DPC=%x FIFOC=%x\n",
           io_read32(AC_BASE + AC_DAC_DG), io_read32(AC_BASE + AC_DAC_DAP_CTR),
           io_read32(AC_BASE + AC_DAC_FIFOS), io_read32(AC_BASE + AC_DAC_CNT),
           io_read32(AC_BASE + AC_DAC_DPC), io_read32(AC_BASE + AC_DAC_FIFOC));
  log_info("v3s probe: PLL_AUDIO=%x GATE2=%x RST3=%x AC_DIG_CLK=%x\n",
           io_read32(CCU_BASE + CCU_PLL_AUDIO_CTRL),
           io_read32(CCU_BASE + CCU_BUS_CLK_GATE2),
           io_read32(CCU_BASE + CCU_BUS_SOFT_RST3),
           io_read32(CCU_BASE + CCU_AC_DIG_CLK));

  /* 1) 模拟侧窗口自检：PR 窗口是经 APB0 的慢速 8 位总线（手册 4.13.5.121），
   * 写完要留建立时间才读得到；读出 3f 才说明模拟寄存器真的写进去了
   * （以前"写完立刻读"拿到 b4/03/70 这种脏值）。 */
  ac_pr_write(AC_PAG_HPV, 0x3f);
  log_info("v3s probe PR: HPV=%02x PA_SRC=%02x HP=%02x (HPV 写入值 3f)\n",
           ac_pr_read(AC_PAG_HPV), ac_pr_read(DAC_PA_SRC), ac_pr_read(HP_CTRL));

  /* 2) DAC 活性自检：填满 TX FIFO → 100ms 内应被抽空、CNT 累加 */
  probe_step("codec alive");

  /* 3) 时钟活性旁证：开 ADC 数字部分，看 RXA_CNT 涨不涨（同一颗 codec、同一条
   * SCLK）。ADC 模拟前端没开时数字部分照样按采样率产出样本，所以它对"时钟有没有
   * 进 codec"是有效的；出现数据 ⇒ codec 内部时钟是活的。 */
  io_write32(AC_BASE + AC_ADC_FIFOC, io_read32(AC_BASE + AC_ADC_FIFOC) | (1u << 28));
  codec_delay_ms(20);
  v = (io_read32(AC_BASE + AC_ADC_FIFOS) >> 8) & 0x3f;
  codec_delay_ms(200);
  {
    u32 f = io_read32(AC_BASE + AC_ADC_FIFOS);
    log_info("v3s probe [ADC alive]: RXA_CNT %d->%d FIFOS=%x %s\n", (int)v,
             (int)((f >> 8) & 0x3f), f,
             ((f >> 8) & 0x3f) != v ? "==> codec 时钟是活的"
                                    : "(ADC 也没数据，模拟前端未开属正常)");
  }
  io_write32(AC_BASE + AC_ADC_FIFOC, io_read32(AC_BASE + AC_ADC_FIFOC) & ~(1u << 28));
}

static void sound_apply_conf(void) {
  /* 【默认关闭】开机自测音只在 /conf/system.conf 里显式写
   *     [sound]
   *     test_tone = 1
   * 时才播放（做面板/耳机 bring-up 时用）。正式启动不发声、不占用时间。 */
  int tone = sysconf_get_int("sound", "test_tone", 0);

  if (tone <= 0) {
    return;
  }
  if (g_snd == NULL) {
    log_warn("v3s codec: tone test skipped (device not inited)\n");
    return;
  }
  codec_probe();
  /* 自测音：三声递升"哔哔哔"（440 → 660 → 880Hz 各 1 秒）。
   * 比单音更容易判断"到底有没有声音"，音高递升也能顺带暴露音高/速度是否成比例偏移
   * （若采样率偏差固定倍数，三声的音程听起来仍然对、但绝对音高会整体升高/降低）。 */
  sound_tone_play(g_snd, 440, 1000);
  sound_tone_play(g_snd, 660, 1000);
  sound_tone_play(g_snd, 880, 1000);

  /* 【采样率实测】统计 1 秒内 DMA 完成回调次数：每次回调 = play_size(4096) 字节
   * = 1024 帧。44.1k 时该值应为 ~43/s；实测明显偏大/偏小就是 PLL/SCLK 与 DAC_FS
   * 档位不匹配（例如 SCLK 误接到 4X 档会正好快 4 倍 ⇒ 声音又尖又快）。
   * 依据：手册 4.13.5.2 DAC_FS 位表 —— SCLK = 512×fs。 */
  {
    u32 c0 = g_dma_irq_count;
    u32 n0 = io_read32(AC_BASE + AC_DAC_CNT);
    u32 c1, n1, frames;
    codec_delay_ms(1000);
    c1 = g_dma_irq_count;
    n1 = io_read32(AC_BASE + AC_DAC_CNT);
    frames = (c1 - c0) * (u32)(g_snd->play_size / 4);
    /* 【判据一·按回调次数估】每次回调 = play_size 字节 ⇒ play_size/4 帧
     *   —— 但如果一次中断并不是搬满 play_size，这个数会偏（实测偏 2 倍）。 */
    kprintf("audio rate: irq=%d/s => %d Hz (估算)\n", (int)(c1 - c0),
            (int)frames);
    /* 【判据二·直接量】AC_DAC_CNT(0x040) 手册 4.13.5.9："TX Sample Counter，
     * DMA 或 CPU 每往 TX FIFO 送进一个样本就 +1"。在 DRQ 节流下"写进去的速率
     * = DAC 抽走的速率"，所以它的 1 秒增量就是真实取样率（16bit 立体声：
     * 帧率 = 样本数/2）。这个判据不依赖任何"每次搬多少字节"的假设。 */
    kprintf("audio rate: CNT_delta=%d samples/s => %d Hz frames\n",
            (int)(n1 - n0), (int)((n1 - n0) / 2));
    kprintf("audio: PLL_AUDIO=%x FIFOC=%x FIFOS=%x ring=%d\n",
            io_read32(CCU_BASE + CCU_PLL_AUDIO_CTRL),
            io_read32(AC_BASE + AC_DAC_FIFOC), io_read32(AC_BASE + AC_DAC_FIFOS),
            (int)ring_size());
  }

  /* 【硬件通路自检·完全绕开 FIFO/DMA】让 DAC 自己输出内部测试波形。
   * 手册 4.13.5.10 AC_DAC_DG(0x048)：
   *   b11 = DAC_MODU_SELECT（1 = 调试模式）
   *   b10:9 = DAC_PATTERN_SELECT（00 = 正常取 FIFO；01 = -6dB 正弦）
   * 进入该模式后 DAC 不再读 TX FIFO，直接按 SCLK/512 产出内部正弦。
   * 判据（这一刀把问题切开）：
   *   · 能听到 ⇒ codec 数字核 + 模拟耳机通路都是好的，问题只在"数据投喂"；
   *   · 听不到 ⇒ 问题在 DAC 之后（模拟侧/耳机座/焊接/静音开关）。 */
  io_write32(AC_BASE + AC_DAC_DG, (1u << 11) | (1u << 9));
  kprintf("dac pattern on: internal -6dB sine 2s (bypass FIFO/DMA)\n");
  codec_delay_ms(2000);
  io_write32(AC_BASE + AC_DAC_DG, 0);
  kprintf("dac pattern off (back to FIFO)\n");
}
#endif /* SYSCONF_MODULE */

/* ---------------- 播放通路 ---------------- */

void sound_play(sound_device_t* dev, void* buf, size_t len) {
  if (dev == NULL || buf == NULL || len == 0) {
    return;
  }

  /* 应用刚写完的缓冲可能还在 D-cache 里，搬运前刷到 PoC */
  cpu_flush_dcache_range((unsigned long)(uintptr_t)buf,
                         (unsigned long)((uintptr_t)buf + len));

  if ((ring_size() + len) > SOUND_BUF_SIZE) {
    /* 严重欠载/突发：丢掉旧数据，否则越写越落后、延迟越来越大 */
    ring_clear();
  }
  ring_write((const u8*)buf, len);

  if (dev->is_play) {
    return; /* DMA 已在跑，回调里继续取数 */
  }

  g_sound_dev = dev;
  /* 【DMA mode 必须带 bit16 —— 就是"应用没声音/卡死"的根因】
   * dma_set_mode()(duck/modules/dma/sunxi-dma.c) 对 mode 的解释是：
   *   bit16 = "流式 DRQ 传输"：置位 ⇒ loop_mode=0（LLI 末包用 SUNXI_DMA_LINK_NULL
   *           收尾）且 wait_cyc=NORMAL_WAIT(8) ⇒ 每传完一块就产生 PKG_END 中断，
   *           中断里回调续投 —— 这才是"DRQ 节流的流式音频"该有的样子；
   *   不置位 ⇒ loop_mode=1：LLI 的 link 指向【自己】（硬件自环重载），且
   *           commit_para=(wait_cyc|data_block_size<<8)=(0|2<<8)=0x0200，
   *           DRQ 等待位为 0 ⇒ DRQ 握手失效。
   * 实测（V3s）：不带 bit16 时 codec 侧完全正常（CPU 直写 FIFO 能进能出、DAC 正常
   * 抽数），但这笔 DMA 永远不产生完成中断 ⇒ 环形缓冲塞满 64KB 后再也不动、tone test
   * 超时。t113-s3.c 那份能出声的驱动传的正是 1 | (1 << 16)。
   * 低 8 位 = 源 DRQ 类型（1 = SDRAM，手册 4.8.2.2 表 4-1 Port 1）；
   * 目的 DRQ 不写 ⇒ 沿用 dma_set_mode() 的默认 DMAC_CFG_TYPE_AUDIO（V3s = Port 15，
   * 手册同表 "Port 15 Audio Codec"）。 */
  dma_init(dev->dma_channel, DMAC_CFG_TYPE_DRAM | (1u << 16), dma_audio_handler,
           dev);

  /* 首次武装：先清零再取数（避免开头把 sound_buf 的旧内容播出去），
   * 刷 cache 后发货（DMA 不查 cache，漏刷就会播旧数据 ⇒ 实测"完全没声音"） */
  kmemset(dev->sound_buf, 0, dev->play_size);
  ring_read((u8*)dev->sound_buf, dev->play_size);
  cpu_flush_dcache_range((unsigned long)(uintptr_t)dev->sound_buf,
                         (unsigned long)((uintptr_t)dev->sound_buf +
                                         dev->play_size));
  dma_trans(dev->dma_channel, dev->sound_buf, (void*)(AC_BASE + AC_DAC_TXDATA),
            dev->play_size);
  dev->is_play = 1;
  log_info("v3s codec: dma start ch=%d chunk=%d\n", dev->dma_channel,
           dev->play_size);
}

void dma_audio_handler(void* data) {
  sound_device_t* dev = (sound_device_t*)data;
  u32 fs;

  /* 中断守卫：DMA 是共享中断、回调槽由各模块共用，可能被以非法指针调用。
   * 约定 0x0000_0000~0x3FFF_FFFF 是 MMIO，内核堆/DRAM 都在 0x4000_0000 以上。 */
  if (dev == NULL || (u32)(unsigned long)dev < 0x40000000u ||
      dev != g_sound_dev) {
    return;
  }
  g_dma_irq_count++; /* 采样率实测用（见文件上方说明） */

#if AUDIO_DBG
  /* 【诊断·前 6 次】用 kprintf 直写串口（log_info 走 log 线程、log_debug 被过滤，
   * 中断里出问题时就看不到）。ring=环形缓冲积压字节、FIFOS=TX FIFO 状态：
   * ring 下降 + FIFO 稳定在触发水位 = DMA 正被 DRQ 节流地搬运。
   * 需要时把本文件顶部的 AUDIO_DBG 置 1。 */
  {
    static u32 dbg_n;
    if (dbg_n < 6u) {
      dbg_n++;
      kprintf("aud irq#%d ring=%d FIFOS=%x\n", (int)dbg_n, (int)ring_size(),
              io_read32(AC_BASE + AC_DAC_FIFOS));
    }
  }
#endif

  /* 清 over/underrun 挂起位（W1C），否则会一直挂着 */
  fs = io_read32(AC_BASE + AC_DAC_FIFOS);
  if (fs & DAC_FIFOS_ERR_BITS) {
    io_write32(AC_BASE + AC_DAC_FIFOS, fs & DAC_FIFOS_ERR_BITS);
  }

  /* 先清零再取数：ring_read 数据不足时只填前半截，尾部残留上一段 PCM，
   * 会被反复重播 ⇒ 嗒嗒声/电流声；清零后尾部恒为静音。 */
  kmemset(dev->sound_buf, 0, dev->play_size);
  ring_read((u8*)dev->sound_buf, dev->play_size);
  cpu_flush_dcache_range((unsigned long)(uintptr_t)dev->sound_buf,
                         (unsigned long)((uintptr_t)dev->sound_buf +
                                         dev->play_size));
  dma_trans(dev->dma_channel, dev->sound_buf, (void*)(AC_BASE + AC_DAC_TXDATA),
            dev->play_size);
}

/* ---------------- 设备接口 ---------------- */

static size_t read(device_t* dev, void* buf, size_t len) { return 0; }

static size_t write(device_t* dev, void* buf, size_t len) {
  sound_play((sound_device_t*)dev->data, buf, len);
  return len;
}

static size_t sound_ioctl(device_t* dev, u32 cmd, void* args) {
  sound_device_t* sound_device = (sound_device_t*)dev->data;
  u32 ret = 0;

  if (cmd == SNDCTL_DSP_GETFMTS) {
    u32* val = (u32*)args;
    *val = AFMT_S16_LE;
  } else if (cmd == SNDCTL_DSP_SETFMT) {
    u32* val = (u32*)args;
    codec_param((int)*val, sound_device->channal, -1);
  } else if (cmd == SNDCTL_DSP_CHANNELS) {
    u32* val = (u32*)args;
    sound_device->channal = (int)*val;
    codec_param(-1, (int)*val, -1);
  } else if (cmd == SNDCTL_DSP_SPEED) {
    u32* val = (u32*)args;
    codec_param(-1, sound_device->channal, (int)*val);
    log_info("v3s codec: speed %d\n", (int)*val);
  } else if (cmd == IOC_STAT || cmd == IOC_STATFS) {
    struct stat* stat = (struct stat*)args;
    stat->st_mode = S_IFCHR;
  }

  return ret;
}

void codec_init(void) {
  log_info("v3s codec init: AC=%x PR=%x\n", AC_BASE, AC_PR_BASE);

  /* codec 的两片寄存器区都要映射（CCU 已由 cpu_clock_init 映射，这里再兜一次） */
  page_map(AC_BASE, AC_BASE, PAGE_DEV);
  page_map(AC_PR_BASE, AC_PR_BASE, PAGE_DEV);
  page_map(CCU_BASE, CCU_BASE, PAGE_DEV);

  codec_ccu_init();
  codec_dac_init();
  codec_analog_init();

  log_info("v3s codec: init end FIFOC=%x DPC=%x\n",
           io_read32(AC_BASE + AC_DAC_FIFOC), io_read32(AC_BASE + AC_DAC_DPC));
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

  /* 【一律静态】设备实例 / DMA 搬运缓冲 / 环形缓冲都在 .bss（见文件上方"中断安全
   * 的内存"）：kmalloc(DEFAULT_TYPE) 出来的是 vm_alloc 的进程私有地址，DMA 中断在
   * 别的线程上下文里会 fault。原来这里是
   *   kmalloc(sizeof(sound_device_t), DEFAULT_TYPE) + kmalloc(..., DEVICE_TYPE)
   *   + buffer_create(...)（fn_malloc ⇒ 同样是 vm_alloc）
   * 其中 sound_buf 那次虽然是 DEVICE_TYPE（phy ✓），但设备结构体本身就已经踩坑。 */
  sound_device_t* sound_device = &g_snd_dev;
  dev->data = sound_device;
  kmemset(sound_device, 0, sizeof(*sound_device));
  sound_device->sound_buf = (char*)g_dma_buf;
  sound_device->buffer = NULL; /* V3s 用静态 ring_*()，不再走 buffer_* */
  sound_device->play_size = SOUND_DMA_CHUNK;
  sound_device->channal = 2;
  sound_device->is_play = 0;
  sound_device->buf_pos = 0;
  sound_device->dma_channel = SOUND_DMA_CHANNEL; /* 与 LCD(0) 隔离 */
  g_snd = sound_device;
#ifdef SYSCONF_MODULE
  /* 登记"配置就绪"回调：/conf/system.conf 的 [sound] test_tone 自测音
   * （sysconf 模块在 fatfs 之后触发；见文件开头的说明） */
  sysconf_on_ready(sound_apply_conf);
#endif

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

  codec_init();

  log_info("sound init end\n");
  return 0;
}

void sound_exit(void) { kprintf("sound exit\n"); }

module_t sound_module = {
    .name = "sound", .init = sound_init, .exit = sound_exit};
