/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
//
// V3s(荔枝派 Zero 等) 片上以太网驱动：EMAC + 内置 10/100M PHY
//
// 【寄存器以 V3s 手册 §8.5 为准】这块 EMAC 是 Allwinner 自家的 EMAC IP（带内部
// DMA，4 字描述符链），**不是** Synopsys DesignWare 的通用寄存器布局：
//   · 复位是 BASIC_CTL_1(0x04) bit0 SOFT_RST（自清零）；
//   · 收发是 TX_CTL_0/RX_CTL_0(0x10/0x24) 的 bit31 EN + TX_CTL_1/RX_CTL_1(0x14/0x28)
//     的 TX_DMA_EN/RX_DMA_EN(bit30) 与 *_DMA_START(bit31，自清零)；
//   · 描述符表地址在 TX_DMA_DESC_LIST(0x20)/RX_DMA_DESC_LIST(0x34)；
//   · MDIO 在 MII_CMD(0x48)：MDC_DIV[22:20]、PHY_ADDR[16:12]、REG[8:4]、WR(bit1)、
//     BUSY(bit0)；数据在 MII_DATA(0x4C)[15:0]。
//   （踩过的坑：一开始按主线 dwmac-sun8i 的名字写成 DW 布局 —— MAC 在 0x0000、
//     DMA 在 0x1000、复位写 DMA_BUS_MODE bit0 —— 结果 `busmode` 读回永远是 1、
//     复位 FSM 不动。主线之所以叫 dwmac-sun8i，是因为它借 stmmac 外壳 + 自定义
//     sun8i DMA ops（其 dma_reset 操作的正是 RX_CTL1/TX_CTL1/RX_FRM_FLT/
//     TX_DMA_DESC_LIST/INT_EN 这些**老式寄存器**），并不代表寄存器是 DW 的。）
//
// 【glue 在 System Control】0x01C00030（手册 §4.4.3.2 EMAC-EPHY Clock Register）：
//   XMII_SEL[27]=0 内部 SMI+MII、PHY_ADDR[24:20]=1（内置 PHY 的 MDIO 地址）、
//   CLK_SEL[18]=1 24MHz、SHUTDOWN[16]=0 上电、PHY_SELECT[15]=1 内部 PHY、
//   EPIT[2]=0 MII。与主线 dwmac-sun8i 的 set_syscon 完全一致。
//
// 【CCU】手册 §4.3.5：EMAC = GATE0(0x060) b17 + RST0(0x2C0) b17；
//   EPHY = GATE4(0x070) b0 + RST2(0x2C8) b2。
//
// 【为什么轮询不用中断】上层 duck/modules/posix/sysfn_net.c 直接
// device_find(DEVICE_NET)->read/write 取原始以太帧，本来就不是阻塞语义。
// 【中断现状（S5）】EMAC 已注册到统一中断框架（irq_register(IRQ_ETHER…)，
// 见本文件 net_init_device 末尾），MAC 的 RX 中断已打开；中断里只做
// W1C 清状态 + 计数/置标志，读包仍走下面的轮询路径 —— 语义等价、零回归，
// 并且 /dev/irq 里能看到 114 的真实计数。要变成纯事件驱动收包，
// 下一步是让 emac_net_read 消费 emac_rx_seen（或改走 irq_request_bh）。
//

#include "arch/cpu.h"
#include "archcommon/io.h"
#include "gpio.h" /* 平台头：IRQ_ETHER（114）等 IRQ 号宏 */
#include "gpio/v3s.h"
#include "kernel/irq.h" /* 统一中断框架：irq_register/irq_enable */
#include "kernel/kernel.h"
#include "v3s-ccu.h"

/* ------------------------------------------------------------------ */
/* 地址与寄存器（手册 §8.5.3 列表，基址 0x01C30000）                     */
/* ------------------------------------------------------------------ */

#define EMAC_BASE 0x01C30000
#define SYSCTL_BASE 0x01C00000
#define EMAC_EPHY_CLK_REG 0x30 /* 手册 §4.4.3.2，默认 0x00058000 */

#define BASIC_CTL_0 0x00    /* b3:2 SPEED(11=100M) b1 LOOPBACK b0 DUPLEX */
#define BASIC_CTL_1 0x04    /* b29:24 BURST_LEN b1 RX_TX_PRI b0 SOFT_RST */
#define INT_STA 0x08        /* 中断状态（只读，W1C 清除） */
#define INT_EN 0x0C         /* 中断使能 */

/* 中断位（手册 §8.5.3 INT_STA/INT_EN） */
#define EMAC_INT_TX (1u << 0) /* 发送完成 */
#define EMAC_INT_RX (1u << 8) /* 收到一帧 */
#define TX_CTL_0 0x10       /* b31 TX_EN */
#define TX_CTL_1 0x14       /* b31 TX_DMA_START b30 TX_DMA_EN b10:8 TX_TH b1 TX_MD */
#define TX_DMA_DESC_LIST 0x20
#define RX_CTL_0 0x24       /* b31 RX_EN b28 STRIP_FCS b27 CHECK_CRC */
#define RX_CTL_1 0x28       /* b31 RX_DMA_START b30 RX_DMA_EN b5:4 RX_TH b1 RX_MD */
#define RX_DMA_DESC_LIST 0x34
#define RX_FRM_FLT 0x38     /* b9 HASH_MULTICAST b8 HASH_UNICAST b1 FLT_MD b0 RX_ALL */
#define MII_CMD 0x48
#define MII_DATA 0x4C
#define ADDR_HIGH_0 0x50    /* MAC 地址高 16 位 */
#define ADDR_LOW_0 0x54     /* MAC 地址低 32 位 */
#define TX_DMA_STA 0xB0
#define RX_DMA_STA 0xC0

/* BASIC_CTL_0 */
#define CTL0_DUPLEX_FULL (1u << 0)
#define CTL0_SPEED_100 (3u << 2) /* 00=1000M 11=100M 10=10M */

/* BASIC_CTL_1 */
#define CTL1_SOFT_RESET (1u << 0) /* 置 1 后硬件自清 */

/* TX_CTL_0 / RX_CTL_0 */
#define TX_EN (1u << 31)
#define RX_EN (1u << 31)
#define RX_STRIP_FCS (1u << 28)
#define RX_CHECK_CRC (1u << 27)

/* TX_CTL_1 / RX_CTL_1 */
#define TX_DMA_START (1u << 31)
#define TX_DMA_EN (1u << 30)
#define TX_MD (1u << 1) /* 1: FIFO 里有一整帧就开始发 */
#define RX_DMA_START (1u << 31)
#define RX_DMA_EN (1u << 30)
#define RX_MD (1u << 1)

/* RX_FRM_FLT */
#define RX_ALL (1u << 0) /* 1: 全收（先打通；将来改按 DA 过滤） */

/* MII_CMD */
#define MII_BUSY (1u << 0)
#define MII_WRITE (1u << 1)
#define MII_ADDR_SHIFT 12
#define MII_REG_SHIFT 4
#define MII_DIV_SHIFT 20
/* AHB 时钟未知 ⇒ MDC 取最大分频（011 = /128），一定不超过 2.5MHz */
#define MII_DIV_SLOWEST (3u << MII_DIV_SHIFT)

/* 描述符（手册 §8.5.5，4 字节字 ×4） */
#define DESC_TX_OWN (1u << 31)   /* w0 bit31: 归 DMA */
#define DESC_TX_INT (1u << 31)   /* w1 bit31: 发完置 TX_INT */
#define DESC_TX_LAST (1u << 30)  /* w1: 帧的最后一个描述符 */
#define DESC_TX_FIRST (1u << 29) /* w1: 帧的第一个描述符 */
#define DESC_TX_SIZE_MASK 0x7FFu /* w1[10:0] */

#define DESC_RX_OWN (1u << 31)  /* w0 bit31: 归 DMA */
#define DESC_RX_FRMLEN_SHIFT 16 /* w0[29:16] 帧长 */
#define DESC_RX_FRMLEN_MASK 0x3FFFu
#define DESC_RX_FIRST (1u << 9)
#define DESC_RX_LAST (1u << 8)
#define DESC_RX_OVERFLOW (1u << 11)
#define DESC_RX_SIZE_MASK 0x7FFu /* w1[10:0] */

#define EMAC_MTU 1600
#define RX_DESC_NUM 8
#define TX_DESC_NUM 4

typedef struct {
  volatile u32 status; /* w0: OWN + 状态 */
  volatile u32 cntl;   /* w1: 长度 + 控制 */
  volatile u32 addr;   /* w2: 缓冲地址 */
  volatile u32 next;   /* w3: 下一个描述符 */
} emac_desc_t;

/* 静态缓冲：内核恒等映射 ⇒ 地址可直接给 DMA 用 */
static emac_desc_t rx_desc[RX_DESC_NUM] __attribute__((aligned(64)));
static emac_desc_t tx_desc[TX_DESC_NUM] __attribute__((aligned(64)));
static u8 rx_buf[RX_DESC_NUM][EMAC_MTU] __attribute__((aligned(64)));
static u8 tx_buf[TX_DESC_NUM][EMAC_MTU] __attribute__((aligned(64)));

typedef struct {
  u8 mac[6];
  u32 phy_addr;
  u32 phy_id;
  u32 link_up;
  u32 speed;
  u32 duplex;
  u32 rx_next;
  u32 tx_next;
  u32 no_link_check; /* 开机自检时跳过 link 判断（loopback 不需要链路） */
  u32 rx_frames, rx_errors, tx_frames, tx_errors, tx_busy;
} v3s_emac_t;

static v3s_emac_t emac;

/* ------------------------------------------------------------------ */
/* 工具                                                                */
/* ------------------------------------------------------------------ */

static inline u32 emac_read(u32 off) { return io_read32(EMAC_BASE + off); }
static inline void emac_write(u32 off, u32 val) { io_write32(EMAC_BASE + off, val); }

static void emac_udelay(u32 us) {
  volatile u32 i;
  u32 loops = us * 200; /* 1GHz 下约 200 次循环/us，宁可多等 */
  for (i = 0; i < loops; i++) {
    __asm__ volatile("nop");
  }
}

static void emac_dcache_flush(void* addr, u32 len) {
  cpu_flush_dcache_range((unsigned long)addr, (unsigned long)addr + len);
}
static void emac_dcache_invalidate(void* addr, u32 len) {
  cpu_invalidate_dcache_range((unsigned long)addr, (unsigned long)addr + len);
}

/* ------------------------------------------------------------------ */
/* MDIO（MII_CMD / MII_DATA）                                          */
/* ------------------------------------------------------------------ */

static int mdio_wait(void) {
  /* 32 个地址扫描时每次最多等 5ms，最坏 ~160ms，不会拖住启动 */
  int t = 500;
  while (t-- > 0) {
    if ((emac_read(MII_CMD) & MII_BUSY) == 0) return 0;
    emac_udelay(10);
  }
  return -1;
}

static int mdio_read(u32 phy, u32 reg, u32* val) {
  u32 cmd = ((phy & 0x1F) << MII_ADDR_SHIFT) | ((reg & 0x1F) << MII_REG_SHIFT);
  emac_write(MII_CMD, cmd | MII_DIV_SLOWEST | MII_BUSY);
  if (mdio_wait() != 0) return -1;
  *val = emac_read(MII_DATA) & 0xFFFF;
  return 0;
}

static int mdio_write(u32 phy, u32 reg, u32 val) {
  u32 cmd = ((phy & 0x1F) << MII_ADDR_SHIFT) | ((reg & 0x1F) << MII_REG_SHIFT);
  emac_write(MII_DATA, val & 0xFFFF);
  emac_write(MII_CMD, cmd | MII_WRITE | MII_DIV_SLOWEST | MII_BUSY);
  return mdio_wait();
}

/* ------------------------------------------------------------------ */
/* 时钟 / 复位 / 内部 PHY 上电                                         */
/* ------------------------------------------------------------------ */

static void emac_clock_init(void) {
  u32 reg;

  /* EMAC 门控 BUS_CLK_GATING_REG0(0x060) b17（手册 §4.3.5.14） */
  reg = io_read32(V3S_CCU_BASE + CCU_BUS_CLK_GATE0);
  io_write32(V3S_CCU_BASE + CCU_BUS_CLK_GATE0, reg | (1u << 17));
  /* EMAC 复位 BUS_SOFT_RST_REG0(0x2C0) b17，1=解除（手册 §4.3.5.42 附近） */
  reg = io_read32(V3S_CCU_BASE + CCU_BUS_SOFT_RST0);
  io_write32(V3S_CCU_BASE + CCU_BUS_SOFT_RST0, reg | (1u << 17));

  /* EPHY 门控 BUS_CLK_GATING_REG4(0x070) b0（手册 §4.3.5.18） */
  reg = io_read32(V3S_CCU_BASE + 0x070);
  io_write32(V3S_CCU_BASE + 0x070, reg | (1u << 0));

  /* EPHY 复位 BUS_SOFT_RST_REG2(0x2C8) b2：按 reset_control_reset 的做法
   * 先 assert 再 de-assert，确保内部 PHY 从干净状态起来 */
  reg = io_read32(V3S_CCU_BASE + 0x2C8);
  io_write32(V3S_CCU_BASE + 0x2C8, reg & ~(1u << 2)); /* assert */
  emac_udelay(1000);
  io_write32(V3S_CCU_BASE + 0x2C8, reg | (1u << 2)); /* de-assert */
  emac_udelay(1000);
}

/* System Control 的 EMAC-EPHY 寄存器（手册 §4.4.3.2，与主线 set_syscon 对齐）：
 *   XMII_SEL  b27=0  内部 SMI + 内部 MII
 *   EPHY_MODE b26:25=0 Normal
 *   PHY_ADDR  b24:20=1 内置 PHY 的 MDIO 地址（主线 dts: ethernet-phy@1）
 *   CLK_SEL   b18=1  24MHz（V3s 24M 晶振）
 *   SHUTDOWN  b16=0  上电（默认 1=关闭）
 *   PHY_SELECT b15=1 用内部 PHY
 *   RMII_EN   b13=0  MII 模式，不用 RMII
 *   EPIT      b2=0   MII（主线 dts: phy-mode = "mii"）
 *   ETCS      b1:0=0 MII 发送时钟
 * b31:28 BPS_EFFUSE 保持原值 */
static void emac_ephy_init(void) {
  u32 reg = io_read32(SYSCTL_BASE + EMAC_EPHY_CLK_REG);

  reg &= 0xF0000000u;
  reg |= (1u << 20); /* PHY_ADDR = 1 */
  reg |= (1u << 18); /* 24MHz */
  reg |= (1u << 15); /* internal PHY */

  io_write32(SYSCTL_BASE + EMAC_EPHY_CLK_REG, reg);
  emac_udelay(1000);
}

/* ------------------------------------------------------------------ */
/* 内部 PHY                                                            */
/* ------------------------------------------------------------------ */

#define MII_BMCR 0x00
#define MII_BMSR 0x01
#define BMCR_RESET (1u << 15)
#define BMCR_SPEED_100 (1u << 13)
#define BMCR_FULL_DUPLEX (1u << 8)
#define BMSR_LINK (1u << 2)

static int phy_scan(void) {
  u32 i, val;

  for (i = 0; i < 32; i++) {
    if (mdio_read(i, MII_BMSR, &val) == 0 && val != 0 && val != 0xFFFF) {
      u32 id1 = 0, id2 = 0;
      mdio_read(i, 2, &id1);
      mdio_read(i, 3, &id2);
      kprintf("emac: found phy at mdio addr %d, id %04x:%04x, bmsr %04x\n", i, id1,
              id2, val);
      return (int)i;
    }
  }
  return -1;
}

static int phy_init(void) {
  u32 val;
  int addr = phy_scan();

  if (addr < 0) {
    kprintf("emac: no phy found on mdio bus (internal ephy not responding)\n");
    return -1;
  }
  emac.phy_addr = (u32)addr;

  mdio_read(emac.phy_addr, 2, &emac.phy_id);
  mdio_read(emac.phy_addr, 3, &val);
  emac.phy_id = (emac.phy_id << 16) | (val & 0xFFFF);

  mdio_write(emac.phy_addr, MII_BMCR, BMCR_RESET);
  for (val = 0; val < 100; val++) {
    u32 bmcr = 0;
    emac_udelay(1000);
    mdio_read(emac.phy_addr, MII_BMCR, &bmcr);
    if ((bmcr & BMCR_RESET) == 0) break;
  }

  /* 先强制 100M 全双工（自协商要 2~3 秒，先确定性拉起来）；要改回自协商就把
   * BMCR 写 ANEG_ENABLE|RESTART，再读 BMSR 的 link 位 */
  mdio_write(emac.phy_addr, MII_BMCR, BMCR_SPEED_100 | BMCR_FULL_DUPLEX);
  emac.speed = 100;
  emac.duplex = 1;

  mdio_read(emac.phy_addr, MII_BMSR, &val);
  kprintf("emac: phy addr %d forced 100M full-duplex, bmsr=%04x\n", addr, val);
  return 0;
}

/* ------------------------------------------------------------------ */
/* MAC / DMA                                                           */
/* ------------------------------------------------------------------ */

/* 软复位：BASIC_CTL_1 bit0 置 1，硬件自清（主线 sun8i_dwmac_reset 同做法） */
static int emac_soft_reset(void) {
  u32 v, t;

  emac_write(BASIC_CTL_1, emac_read(BASIC_CTL_1) | CTL1_SOFT_RESET);
  for (t = 0; t < 1000; t++) {
    v = emac_read(BASIC_CTL_1);
    if ((v & CTL1_SOFT_RESET) == 0) return 0;
    emac_udelay(100);
  }
  return -1;
}

static void emac_set_mac_addr(const u8* mac) {
  emac_write(ADDR_HIGH_0, ((u32)mac[5] << 8) | (u32)mac[4]);
  emac_write(ADDR_LOW_0, ((u32)mac[3] << 24) | ((u32)mac[2] << 16) |
                             ((u32)mac[1] << 8) | (u32)mac[0]);
}

static void emac_desc_init(void) {
  u32 i;

  /* 先刷 RX 缓冲（清掉 cache 脏行）再交给 DMA，否则收到的数据会被脏行盖回去 */
  emac_dcache_flush(rx_buf, sizeof(rx_buf));
  emac_dcache_flush(tx_buf, sizeof(tx_buf));

  for (i = 0; i < RX_DESC_NUM; i++) {
    rx_desc[i].addr = (u32)&rx_buf[i][0];
    rx_desc[i].cntl = EMAC_MTU & DESC_RX_SIZE_MASK;
    rx_desc[i].next = (u32)&rx_desc[(i + 1) % RX_DESC_NUM];
    rx_desc[i].status = DESC_RX_OWN;
  }
  for (i = 0; i < TX_DESC_NUM; i++) {
    tx_desc[i].addr = (u32)&tx_buf[i][0];
    tx_desc[i].cntl = 0;
    tx_desc[i].next = (u32)&tx_desc[(i + 1) % TX_DESC_NUM];
    tx_desc[i].status = 0;
  }
  emac_dcache_flush(rx_desc, sizeof(rx_desc));
  emac_dcache_flush(tx_desc, sizeof(tx_desc));

  emac.rx_next = 0;
  emac.tx_next = 0;
}

/* link up 后打开 MAC 收发与 DMA */
static void emac_start(void) {
  /* 描述符表地址 */
  emac_write(TX_DMA_DESC_LIST, (u32)&tx_desc[0]);
  emac_write(RX_DMA_DESC_LIST, (u32)&rx_desc[0]);

  /* 全收（原始帧直通上层，先不做地址过滤） */
  emac_write(RX_FRM_FLT, RX_ALL);

  /* 100M 全双工 */
  emac_write(BASIC_CTL_0, CTL0_SPEED_100 | CTL0_DUPLEX_FULL);

  /* 开 DMA：EN 常开，START 自清零（FIFO 阈值用默认 64，MD=1 整帧即发） */
  emac_write(TX_CTL_1, TX_DMA_EN | TX_MD);
  emac_write(RX_CTL_1, RX_DMA_EN | RX_MD);
  emac_write(TX_CTL_1, emac_read(TX_CTL_1) | TX_DMA_START);
  emac_write(RX_CTL_1, emac_read(RX_CTL_1) | RX_DMA_START);

  /* 开收发器（剥 FCS + 校验 CRC） */
  emac_write(RX_CTL_0, RX_EN | RX_STRIP_FCS | RX_CHECK_CRC);
  emac_write(TX_CTL_0, TX_EN);

  kprintf("emac: started (ctl0=%x rxctl0=%x txctl1=%x rxctl1=%x sta tx=%x rx=%x)\n",
          emac_read(BASIC_CTL_0), emac_read(RX_CTL_0), emac_read(TX_CTL_1),
          emac_read(RX_CTL_1), emac_read(TX_DMA_STA) & 7, emac_read(RX_DMA_STA) & 7);
}

static void emac_poll_link(void) {
  u32 bmsr = 0;

  if (emac.link_up) return;
  if (mdio_read(emac.phy_addr, MII_BMSR, &bmsr) != 0) return;
  if ((bmsr & BMSR_LINK) == 0) return;

  emac.link_up = 1;
  kprintf("emac: link up (phy %d, bmsr=%04x)\n", emac.phy_addr, bmsr);
}

/* 打印 PHY 的链路/状态寄存器（全志内置 PHY 的速率/双工状态在 0x10，raw 打出来
 * 便于对照；标准 BMSR 见 0x01） */
static void phy_dump_state(const char* tag) {
  u32 bmsr = 0, sts = 0, r11 = 0;

  mdio_read(emac.phy_addr, MII_BMSR, &bmsr);
  mdio_read(emac.phy_addr, 0x10, &sts);
  mdio_read(emac.phy_addr, 0x11, &r11);
  kprintf("emac: phy[%s] bmsr=%04x (link=%d aneg_done=%d) reg10=%04x reg11=%04x\n", tag,
          bmsr, (bmsr & BMSR_LINK) ? 1 : 0, (bmsr >> 5) & 1, sts, r11);
}

/* 【开机自检：MAC 内部 loopback】不需要网线就能验证"描述符环 + DMA + 收发通路"：
 * 打开 BASIC_CTL_0 的 LOOPBACK 位，发一帧 60 字节测试帧，再从 RX 环收回来比对。
 * 通过 ⇒ 硬件收发链路是通的，之后只剩 PHY/链路问题；不通过 ⇒ 直接定位在 DMA/描述符。 */
static size_t emac_net_read(device_t* dev, void* buf, size_t len);
static size_t emac_net_write(device_t* dev, const void* buf, size_t len);

static void emac_loopback_test(device_t* dev) {
  static u8 frame[60];
  static u8 rxframe[EMAC_MTU];
  u32 i, t, got;
  int ok = 0;

  /* dst = broadcast, src = 自己的 MAC, type = 0x88B5（本地实验用类型） */
  for (i = 0; i < 6; i++) frame[i] = 0xFF;
  for (i = 0; i < 6; i++) frame[6 + i] = emac.mac[i];
  frame[12] = 0x88;
  frame[13] = 0xB5;
  for (i = 14; i < sizeof(frame); i++) frame[i] = (u8)(i & 0xFF);

  emac_write(BASIC_CTL_0, emac_read(BASIC_CTL_0) | (1u << 1)); /* LOOPBACK=1 */
  emac.no_link_check = 1;

  emac_net_write(dev, frame, sizeof(frame));

  for (t = 0; t < 200 && !ok; t++) { /* 最多等 ~1s */
    u32 same = 1;
    got = emac_net_read(dev, rxframe, sizeof(rxframe));
    if (got >= sizeof(frame) && rxframe[12] == 0x88 && rxframe[13] == 0xB5) {
      for (i = 14; i < sizeof(frame); i++) {
        if (rxframe[i] != frame[i]) {
          same = 0;
          break;
        }
      }
      if (same) ok = 1;
    }
    emac_udelay(5000);
  }

  emac.no_link_check = 0;
  emac_write(BASIC_CTL_0, emac_read(BASIC_CTL_0) & ~(1u << 1)); /* LOOPBACK=0 */

  kprintf("emac: loopback self-test %s (tx=%d rx=%d err=%d)\n", ok ? "PASS" : "FAIL",
          emac.tx_frames, emac.rx_frames, emac.rx_errors);
}

/* ------------------------------------------------------------------ */
/* 设备接口                                                            */
/* ------------------------------------------------------------------ */

static size_t emac_net_read(device_t* dev, void* buf, size_t len) {
  emac_desc_t* d;
  u32 status, flen;
  int i;

  (void)dev;
  emac_poll_link();
  if (!emac.link_up && !emac.no_link_check) return 0;

  for (i = 0; i < RX_DESC_NUM; i++) {
    d = &rx_desc[emac.rx_next];
    emac_dcache_invalidate(d, sizeof(emac_desc_t));
    status = d->status;
    if (status & DESC_RX_OWN) { /* 还在 DMA 手里 */
      emac.rx_next = (emac.rx_next + 1) % RX_DESC_NUM;
      continue;
    }

    emac_dcache_invalidate((void*)d->addr, EMAC_MTU);

    if (status & DESC_RX_OVERFLOW) {
      emac.rx_errors++;
    } else if (status & DESC_RX_LAST) {
      flen = (status >> DESC_RX_FRMLEN_SHIFT) & DESC_RX_FRMLEN_MASK;
      if (flen > len) flen = len;
      if (flen > 0) {
        kmemcpy(buf, (void*)d->addr, flen);
        emac.rx_frames++;
      }
      d->status = DESC_RX_OWN;
      d->cntl = EMAC_MTU & DESC_RX_SIZE_MASK;
      emac_dcache_flush(d, sizeof(emac_desc_t));
      /* DMA 可能因为"无可用描述符"挂起，补一次 START */
      emac_write(RX_CTL_1, emac_read(RX_CTL_1) | RX_DMA_START);
      emac.rx_next = (emac.rx_next + 1) % RX_DESC_NUM;
      return flen;
    }

    d->status = DESC_RX_OWN;
    d->cntl = EMAC_MTU & DESC_RX_SIZE_MASK;
    emac_dcache_flush(d, sizeof(emac_desc_t));
    emac.rx_next = (emac.rx_next + 1) % RX_DESC_NUM;
  }

  return 0;
}

static size_t emac_net_write(device_t* dev, const void* buf, size_t len) {
  emac_desc_t* d;
  u32 status = DESC_TX_OWN;
  u32 tlen, t;
  u8* dst;

  (void)dev;
  emac_poll_link();
  if (!emac.link_up && !emac.no_link_check) return 0;
  if (len == 0) return 0;
  if (len > EMAC_MTU) len = EMAC_MTU;

  d = &tx_desc[emac.tx_next];
  emac_dcache_invalidate(d, sizeof(emac_desc_t));

  for (t = 0; t < 1000; t++) {
    status = d->status;
    if ((status & DESC_TX_OWN) == 0) break;
    emac_udelay(10);
  }
  if (status & DESC_TX_OWN) {
    emac.tx_busy++;
    return 0; /* 环满，让上层重试 */
  }
  if (status & 0x7FF) emac.tx_errors++; /* w0 低位是发送错误/碰撞状态 */

  dst = (u8*)d->addr;
  tlen = len < 60 ? 60 : len; /* 以太网最小帧 60 字节（不含 FCS，MAC 自己补 FCS） */
  kmemcpy(dst, buf, len);
  if (tlen > len) kmemset(dst + len, 0, tlen - len);
  emac_dcache_flush(dst, tlen);

  d->cntl = (tlen & DESC_TX_SIZE_MASK) | DESC_TX_FIRST | DESC_TX_LAST | DESC_TX_INT;
  d->status = DESC_TX_OWN;
  emac_dcache_flush(d, sizeof(emac_desc_t));

  emac.tx_next = (emac.tx_next + 1) % TX_DESC_NUM;

  /* 拍一下 TX DMA（自清零位） */
  emac_write(TX_CTL_1, emac_read(TX_CTL_1) | TX_DMA_START);
  emac.tx_frames++;

  return len;
}

/* ioctl 命令与 bcm2837.c 保持一致：0x01=MAC 0x02=链路 0x03=速率/双工 */
static int emac_net_ioctl(device_t* dev, u32 cmd, void* args) {
  (void)dev;
  switch (cmd) {
    case 0x01:
      if (args) kmemcpy(args, emac.mac, 6);
      return 0;
    case 0x02:
      /* 既写进 args，也作为返回值：两种调用约定都能拿到状态 */
      if (!emac.link_up) emac_poll_link();
      if (args) *(u32*)args = emac.link_up;
      return emac.link_up ? 1 : 0;
    case 0x03:
      if (args) *(u32*)args = (emac.speed & 0xFFFF) | ((emac.duplex & 0xFFFF) << 16);
      return 0;
    case 0x04:
      if (args) *(u32*)args = emac.phy_id;
      return 0;
    case 0x05:
      if (args) {
        u32* p = (u32*)args;
        p[0] = emac.rx_frames;
        p[1] = emac.rx_errors;
        p[2] = emac.tx_frames;
        p[3] = emac.tx_errors;
        p[4] = emac.tx_busy;
      }
      return 0;
    default:
      return -1;
  }
}

/* ================================================================== */
/* 中断路径（统一中断框架：irq_register / irq_enable）                  */
/* ================================================================== */
/* 【S5】设计文档 §10：EMAC 改中断收包。这里先做"中断只清状态 + 计数/置标志"，
 * 重活（扫描述符环、交付上层）仍由 emac_net_read() 在原来的轮询路径里做 ——
 * 与"直接在 ISR 里做"语义等价，但"什么时候有包"由轮询变成事件驱动，
 * 而且 /dev/irq 里能看到 EMAC 的真实中断计数（S5 的验收点之一）。
 * 中断里**只做 W1C 清状态**：电平型中断不清会立刻重投递 ⇒ 风暴。 */
static volatile u32 emac_irq_count;
static volatile u32 emac_rx_seen;

static int emac_irq(u32 irq, void *arg) {
  u32 sta;

  (void)irq;
  (void)arg;

  sta = emac_read(INT_STA);
  if (sta == 0) {
    return 0; /* 不是我的 ⇒ 共享中断时框架会继续问下一个 handler */
  }
  emac_write(INT_STA, sta); /* 先清状态，再计数 */
  emac_irq_count++;
  if (sta & EMAC_INT_RX) {
    emac_rx_seen = 1;
  }
  return 1; /* 已处理 */
}

int net_init_device(device_t* dev) {
  u32 v;

  kprintf("emac: v3s emac init, base %x\n", EMAC_BASE);

  /* 【必须逐页显式映射】只映射 0x01C30000 时，访问 0x01C31000 会 data abort
   * （实测 pte 1c31000->0），内核线程被当场杀掉。把 EMAC 窗口(64K)全部映射。 */
  for (v = 0; v < 0x10000; v += 0x1000) {
    page_map(EMAC_BASE + v, EMAC_BASE + v, PAGE_DEV);
  }
  page_map(SYSCTL_BASE, SYSCTL_BASE, PAGE_DEV);

  kmemset(&emac, 0, sizeof(emac));
  /* 本地管理地址（02:xx 不属任何厂商）。V3s 没有出厂 MAC，要固定就改这里。 */
  emac.mac[0] = 0x02;
  emac.mac[1] = 0x00;
  emac.mac[2] = 0x00;
  emac.mac[3] = 0x12;
  emac.mac[4] = 0x34;
  emac.mac[5] = 0x56;

  emac_clock_init();
  emac_ephy_init();

  if (emac_soft_reset() != 0) {
    kprintf("emac: soft reset timeout (ctl1=%x)\n", emac_read(BASIC_CTL_1));
    return -1;
  }

  emac_set_mac_addr(emac.mac);
  emac_desc_init();

  /* 轮询：中断全关，状态清一遍 */
  emac_write(INT_EN, 0);
  emac_write(INT_STA, 0xFFFFFFFF);

  dev->data = &emac;
  dev->read = emac_net_read;
  dev->write = emac_net_write;
  dev->ioctl = emac_net_ioctl;

  if (phy_init() != 0) {
    kprintf("emac: phy init failed, device registered but no link\n");
    return -1;
  }

  /* 先把 MAC/DMA 打开（不等链路）：link 只决定"能不能发出去"，
   * 先让 DMA FSM 跑起来，日志里的 sta 才有意义 */
  emac_start();

  /* 【S5】注册到统一中断框架，然后才开 MAC 的 RX 中断。
   * 【顺序不能反】**框架对"未注册的号"的策略是告警一次 + 自动 mask** ——
   * 如果在 irq_register() 之前就开 INT_EN，第一次 RX 就会被当成未注册号
   * 直接 mask 掉，网卡从此收不到中断（和 v3s 上 tick 注册顺序那条坑同源）。 */
  irq_register(IRQ_ETHER, emac_irq, NULL, "emac", IRQ_FLAG_SHARED);
  irq_enable(IRQ_ETHER);
  emac_write(INT_STA, 0xFFFFFFFF); /* 清掉复位以来的历史状态 */
  emac_write(INT_EN, EMAC_INT_RX); /* 只开 RX（TX 仍走轮询，减少中断量） */
  kprintf("emac: irq %d registered, INT_EN=%x\n", IRQ_ETHER,
          emac_read(INT_EN));

  /* 开机最多等 ~1s 链路，并把 PHY 状态打出来（判断网线/自协商用） */
  for (v = 0; v < 200 && !emac.link_up; v++) {
    emac_poll_link();
    if (!emac.link_up) emac_udelay(5000);
  }
  phy_dump_state("boot");

  /* 自检：MAC 内部 loopback，不需要网线 */
  emac_loopback_test(dev);

  kprintf("emac: ready (mac %02x:%02x:%02x:%02x:%02x:%02x) ctl0=%x ctl1=%x syscon=%x "
          "rxflt=%x txdesc=%x rxdesc=%x\n",
          emac.mac[0], emac.mac[1], emac.mac[2], emac.mac[3], emac.mac[4], emac.mac[5],
          emac_read(BASIC_CTL_0), emac_read(BASIC_CTL_1),
          io_read32(SYSCTL_BASE + EMAC_EPHY_CLK_REG), emac_read(RX_FRM_FLT),
          emac_read(TX_DMA_DESC_LIST), emac_read(RX_DMA_DESC_LIST));
  return 0;
}
