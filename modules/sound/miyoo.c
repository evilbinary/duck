/*******************************************************************
 * miyoo (SSD202D) 音频驱动 —— BACH 块
 *
 * 寄存器模型（【之前全错在这里】✗✓，现已逐字对齐 ewokos 真驱动）：
 *   · BACH 寄存器是【32 位宽、4 字节步进】✗，不是 16 位步进 ✗
 *     ewokos: sub_chan_offset = 0x100 + 4 + 0x20*j = 0x104（reader）
 *         EN            0x104 : bit15 en | bit14 init | bit13 trigger |
 *                              bit12 count（水位锁存）| bits11-0 addr_lo
 *         ADDR_HI       0x108 : bits14-0 = MIU 地址 >> 12
 *         SIZE          0x10C : ring 总大小（MIU 单位 = 8 字节）
 *         TRIGGER_LEVEL 0x110 : 本次 queue 的 MIU 增量
 *         OVERRUN_TH    0x114 / UNDERRUN_TH 0x118
 *         LEVEL         0x11C : 当前水位（MIU 单位，先置 count=1 锁存）
 *     我先前按 2 字节步进 ✗ ⇒ addr_hi/size/trigger 全写错位 ✗，
 *     而 **level 读的是一个无关寄存器 ⇒ `lvl` 永远 0** ✗✓——
 *     这就是"管线配好了却一点声没有"的直接原因 ✓
 *   · MIU 单位 = 8 字节：TO_MIUSIZE(x) = x >> 3 ✓
 *   · BACH/audiotop 必须先整块初始化（ewokos msc313_bach_the_horror() ✓）
 *     ⇒ 见机器生成的 miyoo_bach_tables.h（audiotop 64 项 + BACH 380 项 ✓）
 *
 * KISS 约定：只支持 S16LE 双声道（44100/48000…按 src 表 ✓）；64KB 静态 ring；
 * write() 写 ring 后轮询水位等待，不用中断 ✓；写 ring 后必须刷 dcache ✓。
 ********************************************************************/
#include "dev/devfs.h"
#include "dma/dma.h"
#include "kernel/kernel.h"
#include "kernel/schedule.h" /* schedule_get_ticks ✓（先前自己的 extern 与内核声明冲突 ✗） */
#include "sound.h" /* SNDCTL_DSP_x / AFMT_x 等 OSS 常量（照 t113-s3/dummy ✓） */

/* ---- 两个【不同】的物理基址 ✗✓（我第一版就把它们搞混了）----
 * ① 外设/RIU 区 = 0x1F000000 —— ewokos 的 MSC313_BACH(0x2a0400)/BACH_CLK(0x28400)/
 *    BACH_TOP(0x206800) 都是【相对外设区】的偏移（ewokos: mmio_map() 返回 RIU 基址 ✓）。
 *    本内核的 GPIO 驱动用同一个常量（duck/modules/gpio/ssd202d.h:
 *    MS_BASE_REG_RIU_PA = 0x1F000000 ✓，而按键是好用的 ✓）⇒ 这就是正确基址 ✓✓
 * ② DRAM/MIU 区 = 0x20000000 —— 只用来把 ring 的虚拟地址换算成 BACH 要的 MIU 地址 ✓
 *    （FROM/TO_MIUSIZE 那一套 ✓）
 * 【症状】我第一版写成 0x20000000 + 0x2a0400 ⇒ **那是一片 DRAM** ✗ ⇒ 现象正是：
 *   读回永远等于写进去的值 ✓、the_horror 的初值一直“在” ✓、整块寄存器 300ms 里
 *   一个都不变 ✓、不报错也不出声 ✓ —— 因为根本没有硬件 ✗✓
 */
#define PERI_BASE 0x1F000000U                   /* 外设/RIU（= 内核 MS_BASE_REG_RIU_PA ✓） */
#define MIU_BASE 0x20000000U                    /* DRAM/MIU（仅用于 ring 地址换算 ✓） */
#define R_BACH_TOP (PERI_BASE + 0x00206800U)    /* audiotop：模拟/电源部分 */
#define R_BACH_CLK (PERI_BASE + 0x0028400U)     /* 音频时钟开关 */
#define R_BACH (PERI_BASE + 0x002a0400U)        /* BACH 数字部分 */

/* ---- 32 位寄存器访问（BACH 寄存器就是 32 位的 ✓）---- */
static inline void w32(u32 off, u32 v) {
  *(volatile u32 *)(uintptr_t)(R_BACH + off) = v;
}
static inline u32 r32(u32 off) {
  return *(volatile u32 *)(uintptr_t)(R_BACH + off);
}
static inline void wtop32(u32 off, u32 v) {
  *(volatile u32 *)(uintptr_t)(R_BACH_TOP + off) = v;
}

/* ---- BACH 偏移（全部为【字节】偏移 ✓）---- */
#define BACH_SR0_SEL 0x04      /* bits4-7 = src1_sel（采样率表下标 ✓） */
#define BACH_TEST_CTRL7 0x1dc  /* bit15=rd_mono bit14=wr_mono bit13=rd_mono_copy */
#define BACH_DMA_INT 0x21c     /* bit1 = dma_int_en ✓ */
#define CH0 0x100              /* DMA 通道 0（chan_offset = 0x100 + 0x40*i ✓） */
#define CH_CTRL0 0x00          /* 0x100: bit0 rst bit1 en bit2 live_count_en
                                   bit8 rd_int_clear bit10 empty_int_en
                                   bit12 overrun_int_en bit13 underrun_int_en */
#define CH_CTRL8 0x20          /* 0x120: 各种 flag（只读为主） */
#define SUB 0x04               /* 子通道 0 基址 = 0x104 ✓（chan + 4 + 0x20*j ✓） */
#define SUB_EN 0x00            /* 0x104 */
#define SUB_ADDR_HI 0x04       /* 0x108 */
#define SUB_BUFFSZ 0x08        /* 0x10C */
#define SUB_TRIGLVL 0x0c       /* 0x110 */
#define SUB_OVERRUN_TH 0x10    /* 0x114 */
#define SUB_UNDERRUN_TH 0x14   /* 0x118 */
#define SUB_LEVEL 0x18         /* 0x11c */

/* 【开机自检开关】调试期用来"听/数"速率与 mono ✓（已完成使命 ✓）。
 * 它是 5 秒音频，而且跑在【应用第一次 write() 里面】✗ ⇒ 会把应用的音频线程
 * 头一次写就卡 5 秒 ✓（实测确实让 loopwave 播 2 秒就停 ✗）⇒ 默认关掉 ✓。
 * 需要复盘时改回 1 ✓。 */
#define BACH_SELFTEST 0

#define TO_MIUSIZE(x) ((x) >> 3)
#define FROM_MIUSIZE(x) ((x) << 3)

struct reg_val {
  u32 off;
  u32 val;
};
#include "miyoo_bach_tables.h" /* the_horror 的《机器生成》两张表 ✓ */

/* 采样率表（ewokos msc313_bach_src_rates[]，下标即 src1_sel ✓） */
static const int src_rates[] = {8000,  11025, 12000, 16000, 22050,
                                24000, 32000, 44100, 48000};

#define RING_SIZE (192 * 1024)
static u8 snd_ring[RING_SIZE] __attribute__((aligned(64)));
static u32 snd_wr;    /* 应用写到 ring 的字节偏移 */
static int snd_rate = 44100;
static int snd_started = 0;
/* 【应用请求的音频参数 ✗✓】以前 CHANNELS 被忽略 ⇒ 应用送【单声道】我们仍按立体声播
 * ⇒ 每帧吃掉 2 个样本 ⇒ 听感正好【快 2 倍】✓✓（ewokos 是
 * `mono = runtime->channels == 1` 去设 rd_mono 位的 ✓）。现在如实记录并应用 ✓。 */
static int snd_channels = 2;            /* 1=单声道 2=立体声 ✓ */
static int snd_fmt = AFMT_S16_LE;       /* 目前只支持 S16LE ✓ */
static u32 snd_frame_bytes = 4;         /* = channels * 2 ✓（进度模型/节流都按它算 ✓） */

static void snd_update_frame_bytes(void) {
  snd_frame_bytes = (u32)(snd_channels > 0 ? snd_channels : 1) * 2u;
}

/* ---- 播放进度：由驱动自己按【时间】推算 ✗✓（关键修正）----
 * 【为什么必需】硬件那个水位寄存器实测【不是“待播字节数”】✗：环写满后它仍一路涨、
 * 到 ~0x1F9D 就饱和不动（日志：8544→15848→…→64744→64744 ✓）。若拿它算 free ⇒
 * 驱动**不节流** ⇒ 应用把【尚未播出的数据覆盖掉】✗ ⇒ 听感正是跳帧/断续/"像快进" ✓✓。
 * 而硬件的真实播放速率是对的 ✓（实测 32768B / ~417ms ≈ 78KB/s ≈ 22050×4 ✓）。
 * ⇒ 驱动自己算：已播 ≈ 基准 + 经过时间×速率 ✓，把【积压】限制在 BACKLOG_MAX 内 ✓，
 *    这样环里未播的数据永远不会被覆盖 ✓。
 * snd_queued 单调累计（含 ring 回绕 ✓）；推算若追平累计（说明中途断过供 ✓）
 * 就 clamp 并重设基准 ✓。 */
static u32 snd_queued;  /* 累计 queue 的字节 ✓ */
static u32 snd_base_q;  /* 上次基准点时的累计 ✓ */
static u32 snd_base_t;  /* 上次基准点的 tick ✓ */
static u32 snd_backlog; /* 当前积压（未播）字节 ✓ */
#define BACKLOG_MAX (48u * 1024u) /* 允许积压上限：0.54s@22050 立体声 ✓（抗抖动够、延迟不大 ✓） */

/* 按时间推算"已播/积压" ✓（write 与校准都调用 ✓） */
static void play_sync(void) {
  u32 now = schedule_get_ticks();
  u32 played = snd_base_q;
  u32 dt = now - snd_base_t;
  u32 bps = (u32)snd_rate * snd_frame_bytes; /* 按实际通道数 ✓ */
  played += (dt * bps) / 1000u;
  if (played >= snd_queued) { /* 追平累计 ⇒ 中途断过供 ⇒ 重设基准 ✓ */
    played = snd_queued;
    snd_base_q = snd_queued;
    snd_base_t = now;
  }
  snd_backlog = snd_queued - played;
}

/* 【实测速率标定 ✗✓】src1_sel 各档位下硬件【真实】的采样率（Hz；0 = 该档没消费/没测到 ✓）。
 * 为什么必须实测：应用按 22050 送数据，硬件若按 48000 消费 ⇒ 声音"很快" ✓。
 * 表序/时钟分频都不能靠猜 ✗ —— 量一次就知道 ✓（见 bach_measure_hz）。 */
static u32 snd_rate_meas[9];
#define SR_N (sizeof(src_rates) / sizeof(src_rates[0]))

/* 挑 src1_sel 档位：优先用【实测】值 ✓，没标定过才退回标称表 ✓ */
static int bach_pick_index(int rate) {
  int best = 0;
  int i;
  int diff = 0x7fffffff;
  u32 has_meas = 0;
  for (i = 0; i < (int)SR_N; i++) {
    if (snd_rate_meas[i] != 0) {
      has_meas = 1;
    }
  }
  for (i = 0; i < (int)SR_N; i++) {
    int nominal = has_meas ? (int)snd_rate_meas[i] : src_rates[i];
    int d;
    if (has_meas && (int)snd_rate_meas[i] == 0) {
      continue; /* 标定里没消费的档位不参与 ✓ */
    }
    d = nominal - rate;
    if (d < 0) {
      d = -d;
    }
    if (d < diff) {
      diff = d;
      best = i;
    }
  }
  return best;
}

/* 【软件音量 ✗✓】用户反馈"音量太大、有毛刺" ⇒ 典型【数字削顶(clipping)】：
 * the_horror 的增益按"线路电平"给 ✓，而应用送的是满刻度 PCM ✓ ⇒ 过载 ⇒ 又响又毛 ✓。
 * 这里在写环时做简单衰减（右移 = 每 +1 约 -6dB ✓），不动任何硬件寄存器 ✓（零风险）：
 *   SND_VOL_SHIFT = 0 不衰减 / 1 一半(-6dB) / 2 四分之一(-12dB) ✓（想调音量就改这个 ✓） */
#define SND_VOL_SHIFT 1

/* 带衰减的拷贝（S16LE ✓，按 2 字节一个样本处理 ✓） */
static void snd_copy_attn(u8 *dst, const u8 *src, u32 n) {
  u32 i;
  if (SND_VOL_SHIFT == 0) {
    if (n != 0) {
      kmemcpy(dst, src, n);
    }
    return;
  }
  for (i = 0; i + 1u < n; i += 2u) {
    int v = (int)(short)((u16)src[i] | ((u16)src[i + 1u] << 8));
    v >>= SND_VOL_SHIFT;
    dst[i] = (u8)(v & 0xff);
    dst[i + 1u] = (u8)((v >> 8) & 0xff);
  }
}

/* ring 的虚拟地址 → BACH 要的 MIU 地址（DRAM 基址 0x20000000 ✓，不是外设基址 ✗） */
static inline u32 miu_of(u32 addr) { return addr - MIU_BASE; }

/* 短延时：给硬件几个周期就够（ewokos 读水位时只 delay(8) ✓）
 * 【重要 ✗✓】这里原来是 30 万次（≈1ms ✗），而它在 write() 的【热路径】上：
 * 应用按 ~16ms 一包写入 ✓，我们每包却在读水位上花 1~3ms ✗ ⇒ 供给被拖出空隙 ⇒
 * 听感正是"停顿" ✓ ⇒ 热路径必须用这个短的 ✓ */
static void tiny_delay(void) {
  volatile u32 i;
  for (i = 0; i < 2000u; i++) {
  }
}

/* 长延时：只用于【初始化序列】（复位/使能位写入后的等稳定 ✓；ewokos 用 usleep(1000) ✓）
 * —— 启动路径慢一点没关系，绝不能拖慢 write() ✗ */
static void settle_delay(void) {
  volatile u32 i;
  for (i = 0; i < 300000u; i++) {
  }
}

/* the_horror：audiotop + BACH 整块初始化（表由 ewokos 源码机器提取 ✓）
 * 必须在任何播放前做一次；否则 BACH 的 mix/DAC/时钟源都没配 ⇒ 无声 ✗ */
static void the_horror(void) {
  u32 i;
  for (i = 0; i < sizeof(audiotop_init) / sizeof(audiotop_init[0]); i++) {
    wtop32(audiotop_init[i].off, audiotop_init[i].val);
  }
  for (i = 0; i < sizeof(bach_init) / sizeof(bach_init[0]); i++) {
    w32(bach_init[i].off, bach_init[i].val);
  }
  w32(BACH_DMA_INT, r32(BACH_DMA_INT) | (1u << 1)); /* dma_int_en = 1 ✓ */
}

/* 读当前水位（【原始 MIU 单位，不做换算】✓）：count=1 锁存 → 连读两次一致才信 ✓
 * 【注意】LEVEL 在 0x11C ✗——之前读 0x112（无关寄存器）⇒ 永远 0 ✗
 * 另：顺手把原始值也暴露出来，好跟 the_horror 的初始值对照（0xF88 ✗）✓ */
static u32 bach_level_raw(void) {
  u32 last = 0xffffffffu;
  u32 level = 0;
  int tries;
  w32(CH0 + SUB + SUB_EN, r32(CH0 + SUB + SUB_EN) | (1u << 12)); /* count */
  tiny_delay();
  for (tries = 0; tries < 8; tries++) {
    level = r32(CH0 + SUB + SUB_LEVEL) & 0xffffu;
    if (level == last) {
      break;
    }
    last = level;
    tiny_delay();
  }
  w32(CH0 + SUB + SUB_EN, r32(CH0 + SUB + SUB_EN) & ~(1u << 12));
  return level;
}

/* 水位换算成【字节】，并消掉单位的不确定性 ✗✓：
 * 硬件这个水位是 MIU 单位（×8 ✓）还是字节计数 ✓，光看一次读数分不清 ✗
 * ⇒ 若按 ×8 换算后超过整个 ring 大小，那说明它本来就是字节 ✓ 直接采用 ✓。
 * 【为什么必须较真】算错 ⇒ write() 里 free 变 0 ⇒ **每次写都去睡** ⇒ 听感"停顿" ✓✓ */
static u32 bach_level_bytes(void) {
  u32 raw = bach_level_raw();
  u32 bytes = FROM_MIUSIZE(raw);
  if (bytes > RING_SIZE) {
    bytes = raw;
  }
  return bytes;
}

/* 往 ring 里 queue n 字节 —— 照 ewokos msc313_bach_queue_bytes ✓
 *   ① trigger_level = TO_MIUSIZE(n)（【本次增量】✓，不是累计水位 ✗）
 *   ② trigger 位是【翻转】(read → 取反 → write) ✓，不是 1→0 脉冲 ✗ */
static void bach_queue(u32 n) {
  w32(CH0 + SUB + SUB_TRIGLVL, (u32)TO_MIUSIZE(n));
  w32(CH0 + SUB + SUB_EN, r32(CH0 + SUB + SUB_EN) ^ (1u << 13));
  snd_queued += n; /* 累计（含回绕 ✓，单调 ✓） */
}

/* 【实测】把 bytes 字节【静音】queue 进去，量硬件吃掉它花了多少毫秒 ⇒ 真实采样率 ✓
 *   · 帧数 = bytes / 4（S16 立体声，4 字节/帧 ✓）
 *   · Hz = 帧数 * 1000 / ms ✓
 * 返回 0 = 这一档在超时内没吃掉数据（= 该档没在消费 ✗）✓ */
#define CAL_BYTES 8192u
#define CAL_TIMEOUT_MS 800u
static u32 bach_measure_hz(int idx, u32 bytes) {
  u32 i;
  u32 off;
  u32 l0;
  u32 t0;
  u32 ms;
  int done = 0;

  /* 切档：src1_sel = SR0_SEL 的 bits4-7 ✓ */
  w32(BACH_SR0_SEL, (r32(BACH_SR0_SEL) & ~0x00f0u) | ((u32)idx << 4));

  /* 在 ring 的写入位置写静音（不覆盖正在播的自检音 ✓） */
  off = snd_wr;
  for (i = 0; i < bytes; i++) {
    snd_ring[(off + i) % RING_SIZE] = 0;
  }
  cpu_flush_dcache_range((unsigned long)snd_ring,
                         (unsigned long)(snd_ring + RING_SIZE));

  l0 = bach_level_bytes(); /* 记下“这段静音之前”的水位 ✓（单位已归一 ✓） */
  snd_wr = (snd_wr + bytes) % RING_SIZE;
  bach_queue(bytes);

  t0 = schedule_get_ticks();
  while ((schedule_get_ticks() - t0) < CAL_TIMEOUT_MS) {
    /* 【测量偏差 ✗✓】原来这里写 `<= l0 + 1024` ⇒ 还剩 1024 字节没吃完就掐表 ✗
     * ⇒ 只量了 (bytes-1024) 字节，却按 bytes 算速率 ⇒ **系统性高估 8192/7168=1.143** ✗✓
     * —— 之前那串"整体快 114%/时钟偏快"的结论就是这么来的 ✗（25283/1.143 ≈ 22120 ✓
     * 其实和 22050 完全吻合 ✓）。现在收敛到 l0+64 ⇒ 误差 ≤0.8% ✓ */
    if (bach_level_bytes() <= l0 + 64u) { /* 这段基本吃完 ✓ */
      done = 1;
      break;
    }
  }
  ms = schedule_get_ticks() - t0;
  if (!done || ms == 0) {
    return 0; /* 没消费（或快得无法分辨）⇒ 记 0 ✓ */
  }
  return (bytes / (snd_frame_bytes ? snd_frame_bytes : 4u)) * 1000u / ms;
}

/*
 * 【关于"音频时钟"的结论 ✗✓】曾经以为速率整体偏快 ~14%，并对 0x1F028400 的
 * 高字节做过 12 个候选值的实测扫描 —— **实测：速率完全不变** ✗（25283/24975，
 * 差值只是 1ms 量化的噪声 ✓）⇒ 该寄存器与音频时钟无关 ✗；而那个"偏快 114%"
 * 其实是【我发现并修掉的测量偏差】（见 bach_measure_hz ✓）⇒ **音频时钟本来就是
 * 准的** ✓。所以这里不再做时钟扫描 ✓；"停顿"要往【供给断档】方向查 ✓。
 */

static void bach_start(void) {
  u32 miu;
  int i;

  /* ① 音频时钟（ewokos enable_clk ✓） */
  *(volatile u16 *)(uintptr_t)(R_BACH_CLK + 0x0) = 0x00c0;
  *(volatile u8 *)(uintptr_t)(R_BACH_CLK + 0x1c) = 0x01;

  /* ② audiotop + BACH 整块初始化（the_horror ✓）——这是先前完全缺的一步 ✗ */
  the_horror();

  /* ③ 通道 0：静默复位（ewokos open() ✓） */
  w32(CH0 + CH_CTRL0, 1u << 0); /* rst=1, en=0 */
  w32(CH0 + CH_CTRL0, 0);
  w32(CH0 + CH_CTRL0, 1u << 2); /* live_count_en=1 ✓ */
  settle_delay(); /* 初始化路径用【长】延时 ✓（热路径才用短的 ✓） */

  /* ④ 子通道：复位水位（prepare：trigger=0；init=1；init=0 ✓）
   * 注释里点明了：先 trigger=0 是【用到的、别删】✗ */
  w32(CH0 + SUB + SUB_EN, 0);
  w32(CH0 + SUB + SUB_EN, 1u << 14);
  w32(CH0 + SUB + SUB_EN, 0);

  /* ⑤ ring：地址（MIU 地址 >> 3 ✓）、大小、阈值 */
  miu = TO_MIUSIZE(miu_of((u32)(uintptr_t)snd_ring));
  w32(CH0 + SUB + SUB_ADDR_HI, (miu >> 12) & 0x7fffu);
  w32(CH0 + SUB + SUB_EN, (r32(CH0 + SUB + SUB_EN) & ~0x0fffu) | (miu & 0xfffu));
  w32(CH0 + SUB + SUB_BUFFSZ, (u32)TO_MIUSIZE(RING_SIZE));
  w32(CH0 + SUB + SUB_OVERRUN_TH, 0);
  w32(CH0 + SUB + SUB_UNDERRUN_TH, (u32)TO_MIUSIZE(4096));

  /* ⑥ 通道数：按【应用请求】设 rd_mono / rd_mono_copy（照 ewokos mono=channels==1 ✓）
   * bit15 = rd_mono ✓ bit13 = rd_mono_copy ✓；单声道 ⇒ 置 1 ✓ */
  {
    u32 tc7 = r32(BACH_TEST_CTRL7) & ~((1u << 15) | (1u << 13));
    if (snd_channels == 1) {
      tc7 |= (1u << 15) | (1u << 13);
    }
    w32(BACH_TEST_CTRL7, tc7);
  }

  /* ⑦ 采样率档位（src1_sel = SR0_SEL bits4-7 ✓，读改写保留其余位 ✓）
   * 【别用"精确相等"去查表 ✗】snd_rate 可能是【实测值】（如 24674 ✗），精确匹配
   * 会找不到 ⇒ 档位停在 the_horror 留下的 0xF（非法 ✗）。统一走 bach_pick_index ✓ */
  {
    int idx = bach_pick_index(snd_rate);
    w32(BACH_SR0_SEL, (r32(BACH_SR0_SEL) & ~0x00f0u) | ((u32)idx << 4));
  }

  /* ⑧ 中断清标志 + 使能（ewokos trigger=PCM_TRIGER_START ✓）
   * 注释原话：使能 DMA 必须【紧挨在】使能 reader 之前，否则 reader 会锁死 ✗ */
  w32(CH0 + CH_CTRL0, r32(CH0 + CH_CTRL0) | (1u << 8));  /* rd_int_clear=1 */
  w32(CH0 + CH_CTRL0, r32(CH0 + CH_CTRL0) & ~(1u << 8)); /* =0 */
  w32(CH0 + CH_CTRL0, r32(CH0 + CH_CTRL0) | (1u << 1));  /* chan en=1 */
  settle_delay();
  w32(CH0 + SUB + SUB_EN, r32(CH0 + SUB + SUB_EN) | (1u << 15)); /* reader en */
  settle_delay();

  snd_wr = 0;
  /* 进度模型重置 ✓ */
  snd_queued = 0;
  snd_base_q = 0;
  snd_base_t = schedule_get_ticks();
  snd_backlog = 0;
  snd_started = 1;

  log_info("bach start: ring va-miu=%x -> hw(>>3)=%x rate=%d\n",
           miu_of((u32)(uintptr_t)snd_ring),
           TO_MIUSIZE(miu_of((u32)(uintptr_t)snd_ring)), snd_rate);
  /* 【诊断】回读关键寄存器。注意：先把所有值取进局部变量再打印 ✓
   * —— `bach_level_raw()` 会动 EN 寄存器的 count 位 ✗，若写成 log 参数会因
   * 求值顺序不定而污染 suben 的读数 ✓（我上一版 `suben=8600` 就是这么凑巧 ✓）。
   * hw_addr 由 addr_lo | addr_hi<<12 重建，再 <<3 才是真实 MIU 地址 ✓ */
  {
    u32 top00 = *(volatile u32 *)(uintptr_t)(R_BACH_TOP + 0x00);
    u32 sr0 = r32(BACH_SR0_SEL);
    u32 ctrl0 = r32(CH0 + CH_CTRL0);
    u32 suben = r32(CH0 + SUB + SUB_EN);
    u32 sz = r32(CH0 + SUB + SUB_BUFFSZ);
    u32 trig = r32(CH0 + SUB + SUB_TRIGLVL);
    u32 a = (suben & 0x0fffu) | ((r32(CH0 + SUB + SUB_ADDR_HI) & 0x7fffu) << 12);
    log_info("bach regs: top00=%x sr0=%x ctrl0=%x suben=%x hw_addr=%x(byte=%x) "
             "sz=%x trig=%x lvlraw=%x\n",
             top00, sr0, ctrl0, suben, a, a << 3, sz, trig, bach_level_raw());
  }
  /* 【自检方波】441Hz、S16LE 双声道、约 0.37 秒 ⇒ 上电应听到一声短促“哔”。
   * 有哔 ⇒ 整条管线通 ✓（问题只会在应用数据 ✗）；没哔 ⇒ 还是使能/模拟侧 ✗ */
  {
    /* 【速度标尺 ✗✓】不要"感觉"，要能【数出来】：
     * 播 5 组「0.1 秒“嘀” + 0.9 秒静音」= 共【5 秒】音频 ✓
     *   正常(1×) ⇒ 1 秒 1 声、共 5 声 ✓
     *   ~5×      ⇒ 5 声挤在 1 秒内 ✓
     *   ≥10×     ⇒ 连成"嗡嗡"一片，数不出个数 ✓
     * 水位已实测【精确等于 字节/8】（44100B ⇒ raw=0x1588 ✓✓）⇒ 可以用它做流控 ✓✓ */
    u32 fps = (u32)snd_rate;   /* 帧/秒 ✓ */
    u32 total = BACH_SELFTEST ? (fps * 5u) : 0u; /* 关自检 ⇒ 不喂数据 ✓ */
    u32 done = 0;
    log_info("bach selftest: 5 beeps over 5s (1 beep/s), tc7=%x — 请数”嘀“的个数 "
             "与总时长 ✓\n",
             r32(BACH_TEST_CTRL7));
    while (done < total) {
      u32 chunk = 2048u; /* 一次 2048 帧 ≈ 8KB ✓ */
      u32 i;
      if (chunk > total - done) {
        chunk = total - done;
      }
      for (i = 0; i < chunk; i++) {
        u32 f = done + i;
        u32 within = f % fps;                       /* 一秒内的位置 ✓ */
        u32 o = (snd_wr + i * snd_frame_bytes) % RING_SIZE;
        int v;
        if (within < (fps / 10u)) {                 /* 前 0.1 秒：蜂鸣 ✓ */
          v = ((f / 50u) & 1u) ? 7900 : -7900;
        } else {
          v = 0;                                    /* 其余 0.9 秒：静音 ✓ */
        }
        snd_ring[o] = (u8)(v & 0xff);
        snd_ring[(o + 1u) % RING_SIZE] = (u8)((v >> 8) & 0xff);
        if (snd_channels != 1) { /* 立体声才写右声道 ✓ */
          snd_ring[(o + 2u) % RING_SIZE] = (u8)(v & 0xff);
          snd_ring[(o + 3u) % RING_SIZE] = (u8)((v >> 8) & 0xff);
        }
      }
      cpu_flush_dcache_range((unsigned long)snd_ring,
                             (unsigned long)(snd_ring + RING_SIZE));
      snd_wr = (snd_wr + chunk * snd_frame_bytes) % RING_SIZE;
      bach_queue(chunk * snd_frame_bytes);
      done += chunk;
      /* 【按水位流控】只堆到半个环 ✓（水位单位已实测确认 ✓✓） */
      while (bach_level_bytes() > (RING_SIZE / 2u)) {
        thread_sleep(thread_current(), 1);
      }
      /* 每 1 秒音频打一行（日志间隔也能配合听感反映真实倍率 ✓） */
      if ((done % fps) == 0u) {
        log_info("bach selftest: fed %d/%d frames (%d sec of audio)\n", (int)done,
                 (int)total, (int)(done / fps));
      }
    }
    log_info("bach lvl right after selftest(fed %d B): raw=%x\n",
             (int)(total * snd_frame_bytes), bach_level_raw());

    /* 【让自检音先放完】✓（免得标定用的静音与它混在一起，听感无法判断 ✓） */
    {
      u32 t0 = schedule_get_ticks();
      while ((schedule_get_ticks() - t0) < 1500u && bach_level_bytes() > 4096u) {
      }
    }
    /* 【实测速率】只量【当前档位】一次 ✓
     * （上一版 9 档全扫已经确认："索引→速率表是对的 ✓，但整体偏快 ~12% ✗"，
     *   属于【时钟分频】问题 ✗ ⇒ 不必每次启动都扫 ✓，扫 9 次本身也拖启动 ✓） */
    {
      int idx = bach_pick_index(snd_rate);
      u32 hz;
      w32(BACH_SR0_SEL, (r32(BACH_SR0_SEL) & ~0x00f0u) | ((u32)idx << 4));
      hz = bach_measure_hz(idx, CAL_BYTES);
      if (hz != 0) {
        snd_rate_meas[idx] = hz;
      }
      log_info("bach rate: want=%d -> idx=%d meas=%d Hz (nominal %d, ratio %d%%)\n",
               snd_rate, idx, (int)hz, src_rates[idx],
               (int)(hz * 100u / (u32)src_rates[idx]));
    }
    /* 【flag 解码】0x120 = CH_CTRL8 ✓ */
    {
      u32 f = r32(CH0 + CH_CTRL8);
      log_info("bach r120=%08x flags: rd_underrun=%d rd_overrun=%d rd_empty=%d "
               "wr_full=%d rd_localbuf_empty=%d\n",
               f, (int)((f >> 2) & 1), (int)((f >> 3) & 1), (int)((f >> 4) & 1),
               (int)((f >> 5) & 1), (int)((f >> 7) & 1));
    }
    log_info("bach lvl: selftest=%d raw_now=%x\n",
             (int)(total * snd_frame_bytes), bach_level_raw());
    /* 【进度模型清零】自检音 + 校准用的数据都算"已过去" ✓
     * ⇒ 从这里开始算应用的真实积压 ✓（否则模型会把自检音当成待播 ✓） */
    play_sync();
    snd_queued = 0;
    snd_base_q = 0;
    snd_base_t = schedule_get_ticks();
    snd_backlog = 0;
  }
}

/* 【OSS ioctl】照 t113-s3.c 的骨架 ✓：GETFMTS 必须答 AFMT_S16_LE，
 * 否则 SDL 报 "Couldn't find any hardware audio formats" ✗；
 * IOC_STAT/IOC_STATFS 要给 S_IFCHR ✓（应用会 stat 设备 ✗）。 */
size_t sound_ioctl(device_t *dev, u32 cmd, void *args) {
  u32 ret = 0;
  (void)dev;
  if (cmd == SNDCTL_DSP_GETFMTS) {
    u32 *val = args;
    if (val) {
      *val = AFMT_S16_LE;
    }
  } else if (cmd == SNDCTL_DSP_SETFMT) {
    u32 *val = args;
    if (val) {
      snd_fmt = (int)*val;
    }
    ret = val ? *val : 0; /* 只声明支持 S16LE ✓，值原样返回 ✓ */
    log_info("dsp SETFMT=%x (want S16LE=%x)\n", snd_fmt, AFMT_S16_LE);
  } else if (cmd == SNDCTL_DSP_CHANNELS) {
    /* 【必须当真 ✗✓】应用要 1 声道 ⇒ 硬件也得按单声道播 ✓，否则快 2 倍 ✓ */
    u32 *val = args;
    if (val) {
      snd_channels = (*val == 1) ? 1 : 2;
      snd_update_frame_bytes();
      log_info("dsp CHANNELS=%d -> mono=%d frame_bytes=%d\n", (int)*val,
               snd_channels == 1, (int)snd_frame_bytes);
    }
  } else if (cmd == SNDCTL_DSP_SPEED) {
    u32 *val = args;
    if (val) {
      u32 rate = *val;
      /* 【用实测标定挑档位】✓（没标定过就退回标称表 ✓）
       * 【注意 ✗✓】snd_rate 必须保留【应用请求的】数值 ✓ —— 它是后续
       * “把时钟调到多少才准”的【目标】✓；不能把它改写成实测值 ✗，
       * 否则时钟校准会去追一个本身就偏快的目标 ✗（越校越快 ✗）。 */
      int best = bach_pick_index((int)rate);
      snd_rate = (int)rate;
      if (snd_started) { /* 已在跑 ⇒ 换档即可（KISS：不打断播放 ✓） */
        w32(BACH_SR0_SEL, (r32(BACH_SR0_SEL) & ~0x00f0u) | ((u32)best << 4));
      }
    }
  } else if (cmd == 8 /* IOC_STAT */ || cmd == 9 /* IOC_STATFS */) {
    /* 结构体第二个字段是 mode（照 t113-s3.c 的用法 ✓） */
    u32 *fields = args;
    if (fields) {
      fields[1] = 0x2000; /* S_IFCHR */
    }
  }
  return ret;
}

static size_t read(device_t *dev, void *buf, size_t len) {
  (void)dev;
  (void)buf;
  (void)len;
  return 0;
}

/* 【write：非阻塞 ✗✓ 照 v3s.c 的写法】
 * v3s（本仓库里工作正常的驱动 ✓）是：
 *     write() → sound_play()（塞进环 + 首次武装 DMA）→ return len ✓ 从不等待 ✓
 *     环满了就 ring_clear()【丢旧的】✓ 绝不 sleep ✗
 * 我之前给 miyoo 写的是"积压满就 thread_sleep 等"✗ ⇒ 应用的音频线程会被挂死 ✓
 * ⇒ 环里缓冲播完就"一下断"✓✓（现象完全吻合 ✓）。现在改为纯非阻塞 ✓。
 * 节奏由应用自己掌握（SDL 音频线程按片长自己算延时 ✓，v3s 上就是这么跑的 ✓）。 */
static size_t write(device_t *dev, const void *buf, size_t len) {
  const u8 *p = (const u8 *)buf;
  u32 n = (u32)len;

  (void)dev;
  if (snd_started == 0) {
    bach_start();
  }
  if (n == 0) {
    return 0;
  }
  if (n > RING_SIZE) { /* 一次超过一整环 ⇒ 只留最后一环（同 v3s 的丢旧 ✓） */
    p += (n - RING_SIZE);
    n = RING_SIZE;
  }

  /* 【照 t113-s3 的语义：宁可丢输入，也绝不覆盖未播数据 ✗✓】关键修复
   * t113-s3 的环是"固定小块 DMA + 中断续投"⇒ 真正的 FIFO ⇒ 环满只丢数据 ✓，
   * 硬件正在播的位置绝不会被顶掉 ✓。
   * 我们 miyoo 是【硬件直接读整个环】✗ ⇒ 若环满仍继续写 ⇒ 覆盖尚未播出的数据
   * ⇒ 听感就是"前一段还没播完，就被插进下一段" ✓✓（用户现象 ✓）。
   * 这里改成：只写"按时间模型算出来的空闲空间"那么多 ✓，超出部分【丢掉】✗，
   * 而且丢的是【输入的前段】（尽量贴近实时 ✓）。绝不阻塞、绝不覆盖 ✗。 */
  {
    u32 freeb;
    play_sync();
    freeb = (snd_backlog >= RING_SIZE) ? 0u : (RING_SIZE - snd_backlog);
    if (n > freeb) {
      u32 drop = n - freeb;
      static u32 drop_dbg = 0;
      if (drop_dbg < 10u) {
        drop_dbg++;
        log_warn("bach wr drop-new %d of %d B (backlog=%d)\n", (int)drop,
                 (int)n, (int)snd_backlog);
      }
      p += drop;
      n = freeb;
    }
    if (n == 0) {
      return len; /* 环已满 ⇒ 这批全丢 ✓（非阻塞 ✓，应用不会卡 ✓） */
    }
  }
  n &= ~1u; /* 按 2 字节（一个 16bit 样本）对齐 ✓，保证样本不会被拆到环两端 ✓ */

  /* 写入环（可能跨环尾 ✓） */
  {
    u32 first = RING_SIZE - snd_wr;
    if (first > n) {
      first = n;
    }
    first &= ~1u; /* 样本对齐 ✓（snd_wr 本身也是 2 的倍数 ✓） */
    snd_copy_attn(snd_ring + snd_wr, p, first); /* 带音量衰减 ✓ */
    if (n > first) {
      snd_copy_attn(snd_ring, p + first, n - first);
    }
    /* DMA 直读内存 ⇒ 写完必须刷 cache ✓（DMA 不看 cache ✓，漏刷会播旧数据 ✓） */
    {
      u32 tail = snd_wr + n;
      if (tail > RING_SIZE) {
        cpu_flush_dcache_range((unsigned long)(snd_ring + snd_wr),
                               (unsigned long)(snd_ring + RING_SIZE));
        cpu_flush_dcache_range((unsigned long)snd_ring,
                               (unsigned long)(snd_ring + (tail - RING_SIZE)));
      } else {
        cpu_flush_dcache_range((unsigned long)(snd_ring + snd_wr),
                               (unsigned long)(snd_ring + tail));
      }
    }
    snd_wr = (snd_wr + n) % RING_SIZE;
    bach_queue(n); /* 敲一下 trigger 告诉硬件有新数据 ✓ */
  }
  snd_queued += 0; /* 已在 bach_queue 里累计 ✓ */

  return len; /* ★ 永远收下并返回 len（绝不短写、绝不阻塞 ✓） */
}

void codec_init(void) {
  /* 【设备寄存器页映射】照 t113-s3 的 page_map(CODEC_BASE, CODEC_BASE, PAGE_DEV) ✓。
   * 放在驱动初始化（只做一次 ✓），而不是每次播放的 bach_start ✓。
   * 恒等映射只覆盖 RAM，外设寄存器区间不在页表里 ✗
   * （fault 日志 `pte 28000 -> 0` 就是证据 ✓）。按页对齐映射三个区间 ✓。
   * 注意基址是【外设区 0x1F000000】✗ 不是 DRAM ✗（见文件开头的踩坑记录 ✓）。 */
  page_map(R_BACH & ~0xfffU, R_BACH & ~0xfffU, PAGE_DEV); /* BACH 数字 0x1F2a0400 ✓ */
  page_map(R_BACH_TOP & ~0xfffU, R_BACH_TOP & ~0xfffU, PAGE_DEV); /* audiotop 0x1F206800 ✓ */
  page_map(R_BACH_CLK & ~0xfffU, R_BACH_CLK & ~0xfffU, PAGE_DEV); /* 时钟 0x1F028400 ✓ */
}

int sound_init(void) {
  device_t *dev = kmalloc(sizeof(device_t), DEFAULT_TYPE);
  vnode_t *dsp;
  vnode_t *dsp0;

  log_info("sound init (BACH: 32bit regs + the_horror)\n");
  dev->name = "sound";
  dev->read = read;
  dev->write = write;
  dev->ioctl = sound_ioctl; /* GETFMTS/STAT 等必须应答 ✓（否则 SDL 报无硬件格式 ✗） */
  dev->id = DEVICE_SB;
  dev->type = DEVICE_TYPE_BLOCK;
  device_add(dev);

  dsp = vfs_create_node("dsp", V_FILE | V_BLOCKDEVICE);
  dsp->device = device_find(DEVICE_SB);
  dsp->op = &device_operator;
  vfs_mount(NULL, "/dev", dsp);

  /* 照 t113-s3.c：再挂一个 dsp0 ✓（有的应用/SDL 变体会找它 ✓） */
  dsp0 = vfs_create_node("dsp0", V_FILE | V_BLOCKDEVICE);
  dsp0->device = device_find(DEVICE_SB);
  dsp0->op = &device_operator;
  vfs_mount(NULL, "/dev", dsp0);

  codec_init();
  return 0;
}

void sound_exit(void) { kprintf("sound exit\n"); }

module_t sound_module = {
    .name = "sound", .init = sound_init, .exit = sound_exit};
