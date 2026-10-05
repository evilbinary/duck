/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
/* Raspberry Pi 5 网口：RP1 上的 Cadence GEM（compatible raspberrypi,rp1-gem）。
 *
 * 出站窗口和 RP1 BAR 按 raspi5 的 0x1F00000000
 * 对应 PCIe 地址 0。GEM 在 +0x100000，时钟在 +0x18000。固件没开这扇窗口时，
 * 读寄存器会得到 0xdeaddead。DMA 地址加 0x1000000000，对应入站 BAR2。
 * 上层轮询原始以太帧，不开中断。
 */
#include "arch/cpu.h"
#include "kernel/kernel.h"
#include "kernel/page.h"

#define RP1_CPU_BASE 0x1F00000000ULL
#define GEM_BASE (RP1_CPU_BASE + 0x100000ULL)
#define GEM_SIZE 0x4000u
#define CLK_BASE (RP1_CPU_BASE + 0x18000ULL)
/* GPIO32：bank1（从 28 起）的第 4 脚，低有效 PHY 复位。 */
#define PHY_RST_CTRL (RP1_CPU_BASE + 0xD0000ULL + 0x4000ULL + 4 * 8 + 4)
#define PHY_RST_PAD (RP1_CPU_BASE + 0xF0000ULL + 0x4004ULL + 4 * 4)
/* 固件入站窗口：PCIe 0x10_00000000 对应内存 0。 */
#define GEM_DMA_ADD 0x1000000000ULL

#define CLK_ETH_CTRL 0x064u
#define CLK_ETH_DIV 0x068u
#define CLK_ETH_SEL 0x070u
#define CLK_ETH_TSU_CTRL 0x134u
#define CLK_ETH_TSU_DIV 0x138u
#define CLK_ETH_TSU_SEL 0x140u
#define CLK_CTRL_ENABLE (1u << 11)
#define CLK_CTRL_AUXSRC_SHIFT 5

#define GEM_NCR 0x0000
#define GEM_NCFGR 0x0004
#define GEM_NSR 0x0008
#define GEM_USRIO 0x000c
#define GEM_DMACFG 0x0010
#define GEM_TSR 0x0014
#define GEM_RBQP 0x0018
#define GEM_TBQP 0x001c
#define GEM_RSR 0x0020
#define GEM_IDR 0x002c
#define GEM_MAN 0x0034
#define GEM_SA1B 0x0088
#define GEM_SA1T 0x008c
#define GEM_MID 0x00fc
#define GEM_DCFG1 0x0280
#define GEM_DCFG6 0x0294
#define GEM_TBQPH 0x04c8
#define GEM_RBQPH 0x04d4

#define NCR_RE (1u << 2)
#define NCR_TE (1u << 3)
#define NCR_MPE (1u << 4)
#define NCR_TSTART (1u << 9)
#define NCR_MIIONRGMII (1u << 28)
#define USRIO_RGMII (1u << 0)
#define NSR_IDLE (1u << 2)
#define NCFGR_SPD (1u << 0)
#define NCFGR_FD (1u << 1)
#define NCFGR_CAF (1u << 4)
#define NCFGR_GBE (1u << 10)
#define NCFGR_CLK_SHIFT 18
#define NCFGR_CLK_DIV96 5u
#define NCFGR_DBW_SHIFT 21
#define DMACFG_FBLDO_16 16u
#define DMACFG_RXBMS_FULL (3u << 8)
#define DMACFG_TXPBMS (1u << 10)
#define DMACFG_RXBS_SHIFT 16
#define DMACFG_ADDR64 (1u << 30)
#define DCFG6_DAW64 (1u << 23)

#define RX_USED (1u << 0)
#define RX_WRAP (1u << 1)
#define RX_SOF (1u << 14)
#define RX_EOF (1u << 15)
#define TX_LAST (1u << 15)
#define TX_WRAP (1u << 30)
#define TX_USED (1u << 31)

#define MII_BMCR 0
#define MII_BMSR 1
#define MII_PHYSID1 2
#define MII_PHYSID2 3
#define MII_ADVERTISE 4
#define MII_CTRL1000 9
#define MII_STAT1000 10
#define ADVERTISE_ALL 0x01e1u
#define ADVERTISE_1000FULL 0x0200u
#define ADVERTISE_1000HALF 0x0100u
#define BMCR_RESET (1u << 15)
#define BMCR_ANENABLE (1u << 12)
#define BMCR_ANRESTART (1u << 9)
#define BMSR_LSTATUS (1u << 2)
#define LPA_1000FULL (1u << 11)
#define LPA_1000HALF (1u << 10)

#define GEM_MTU 2048
#define RX_NUM 8
#define TX_NUM 4

typedef struct {
  volatile u32 addr;
  volatile u32 ctrl;
  volatile u32 addrh;
  volatile u32 rsvd;
} gem_desc_t;

typedef struct {
  u8 mac[6];
  u32 phy;
  u32 have_phy;
  u32 phy_id;
  u32 link_up;
  u32 speed;
  u32 duplex;
  u32 ready;
  u32 dma64;
  u32 rx_next;
  u32 tx_next;
  u32 rx_frames, rx_errors, tx_frames, tx_errors, tx_busy;
} rp1_gem_t;

static rp1_gem_t gem;
static gem_desc_t rx_desc[RX_NUM] __attribute__((aligned(64)));
static gem_desc_t tx_desc[TX_NUM] __attribute__((aligned(64)));
static u8 rx_buf[RX_NUM][GEM_MTU] __attribute__((aligned(64)));
static u8 tx_buf[TX_NUM][GEM_MTU] __attribute__((aligned(64)));

static inline u32 gem_read(u32 off) {
  return *(volatile u32*)(uintptr_t)(GEM_BASE + off);
}
static inline void gem_write(u32 off, u32 val) {
  *(volatile u32*)(uintptr_t)(GEM_BASE + off) = val;
  asm volatile("dsb sy" ::: "memory");
}
static inline u32 clk_read(u32 off) {
  return *(volatile u32*)(uintptr_t)(CLK_BASE + off);
}
static inline void clk_write(u32 off, u32 val) {
  *(volatile u32*)(uintptr_t)(CLK_BASE + off) = val;
  asm volatile("dsb sy" ::: "memory");
}

static void gem_udelay(u32 us) {
  volatile u32 i;
  u32 n = us * 50;
  for (i = 0; i < n; i++) {
    asm volatile("nop");
  }
}

static void gem_map(u64 base, u32 size) {
  u64 addr;
  u64 end = base + size;
  for (addr = base & ~0xFFFULL; addr < end; addr += 0x1000) {
    page_map((vaddr_t)addr, (vaddr_t)addr, PAGE_DEV);
  }
}

static void gem_clock_on(u32 ctrl, u32 div, u32 sel) {
  u32 c = clk_read(ctrl);
  /* CLK_ETH 没有标准父时钟。aux0 是 pll_sys_sec，125MHz。
   * RP1 要求 tx_clk 一直是 125MHz，100M 由 GEM 自己分频。
   * SEL 的 bit0 选中 aux，AUXSRC 保持 0。 */
  clk_write(div, 1);
  clk_write(sel, 1);
  c &= ~(0x1fu << CLK_CTRL_AUXSRC_SHIFT);
  c |= CLK_CTRL_ENABLE;
  clk_write(ctrl, c);
}

static int mdio_wait(void) {
  int t = 500;
  while (t-- > 0) {
    if (gem_read(GEM_NSR) & NSR_IDLE) return 0;
    gem_udelay(10);
  }
  return -1;
}

static u32 mdio_pack(u32 phy, u32 reg, u32 op, u32 data) {
  return (1u << 30) | (op << 28) | ((phy & 0x1f) << 23) | ((reg & 0x1f) << 18) |
         (2u << 16) | (data & 0xffff);
}

static int mdio_read(u32 phy, u32 reg, u32* val) {
  if (mdio_wait() != 0) return -1;
  gem_write(GEM_MAN, mdio_pack(phy, reg, 2, 0));
  if (mdio_wait() != 0) return -1;
  *val = gem_read(GEM_MAN) & 0xffff;
  return 0;
}

static int mdio_write(u32 phy, u32 reg, u32 val) {
  if (mdio_wait() != 0) return -1;
  gem_write(GEM_MAN, mdio_pack(phy, reg, 1, val));
  return mdio_wait();
}

static int phy_find(void) {
  u32 i, bmsr, id1, id2;
  for (i = 0; i < 32; i++) {
    bmsr = 0;
    if (mdio_read(i, MII_BMSR, &bmsr) != 0) continue;
    if (bmsr == 0 || bmsr == 0xffff || bmsr == 0xdead) continue;
    id1 = 0;
    id2 = 0;
    mdio_read(i, MII_PHYSID1, &id1);
    mdio_read(i, MII_PHYSID2, &id2);
    gem.phy = i;
    gem.have_phy = 1;
    gem.phy_id = (id1 << 16) | id2;
    kprintf("gem: phy %u id %04x:%04x bmsr %04x\n", i, id1, id2, bmsr);
    return 0;
  }
  kprintf("gem: no phy\n");
  return -1;
}

static void gem_apply_link(void);

static void phy_poll(void) {
  u32 bmsr = 0, gstat = 0, bmcr = 0;
  u32 prev_link, prev_speed, prev_duplex;
  if (!gem.have_phy) return;
  prev_link = gem.link_up;
  prev_speed = gem.speed;
  prev_duplex = gem.duplex;
  if (mdio_read(gem.phy, MII_BMSR, &bmsr) != 0) return;
  mdio_read(gem.phy, MII_BMSR, &bmsr);
  if (bmsr == 0xffff || bmsr == 0xdead || (bmsr & BMSR_LSTATUS) == 0) {
    gem.link_up = 0;
    gem.speed = 0;
  } else {
    /* BCM54213 协商结果在辅助状态 0x19 的 [10:8]，不是 BMCR。
     * 自动协商时 BMCR 的速率位保持 0，之前被当成 10M，MAC 和 PHY 对不上。 */
    mdio_read(gem.phy, 0x19, &bmcr);
    gem.link_up = 1;
    switch ((bmcr >> 8) & 7) {
      case 7:
        gem.speed = 1000;
        gem.duplex = 1;
        break;
      case 6:
        gem.speed = 1000;
        gem.duplex = 0;
        break;
      case 5:
        gem.speed = 100;
        gem.duplex = 1;
        break;
      case 3:
        gem.speed = 100;
        gem.duplex = 0;
        break;
      case 2:
        gem.speed = 10;
        gem.duplex = 1;
        break;
      case 1:
        gem.speed = 10;
        gem.duplex = 0;
        break;
      default:
        mdio_read(gem.phy, MII_STAT1000, &gstat);
        mdio_read(gem.phy, 5, &bmcr);
        gem.duplex = 1;
        if (gstat & LPA_1000FULL) {
          gem.speed = 1000;
        } else if (gstat & LPA_1000HALF) {
          gem.speed = 1000;
          gem.duplex = 0;
        } else if (bmcr & 0x0100) {
          gem.speed = 100;
        } else if (bmcr & 0x0080) {
          gem.speed = 100;
          gem.duplex = 0;
        } else {
          gem.speed = 10;
          gem.duplex = (bmcr & 0x0040) ? 1 : 0;
        }
        break;
    }
  }
  if (gem.ready && (gem.link_up != prev_link || gem.speed != prev_speed ||
                    gem.duplex != prev_duplex)) {
    gem_apply_link();
  }
}

/* DCFG1 的总线宽度：4=128 位，2=64 位。配成 32 位时，描述符回写只留下
 * 占用位，状态字保持 0。 */
static u32 gem_ncfgr_dbw(void) {
  u32 def = (gem_read(GEM_DCFG1) >> 25) & 7u;
  if (def == 4) return 2u << NCFGR_DBW_SHIFT;
  if (def == 2) return 1u << NCFGR_DBW_SHIFT;
  return 0;
}

static void gem_apply_link(void) {
  u32 ncr = gem_read(GEM_NCR);
  u32 cfg = (NCFGR_CLK_DIV96 << NCFGR_CLK_SHIFT) | NCFGR_CAF | gem_ncfgr_dbw();
  if (gem.duplex) cfg |= NCFGR_FD;
  if (gem.speed == 1000) {
    cfg |= NCFGR_GBE;
  } else if (gem.speed == 100) {
    cfg |= NCFGR_SPD;
  }
  if (ncr & (NCR_RE | NCR_TE)) gem_write(GEM_NCR, ncr & ~(NCR_RE | NCR_TE));
  gem_write(GEM_NCFGR, cfg);
  if (ncr & (NCR_RE | NCR_TE)) gem_write(GEM_NCR, ncr);
}

static u32 gem_phys_lo(void* p) {
  return (u32)((u64)(uintptr_t)p + GEM_DMA_ADD);
}
static u32 gem_phys_hi(void* p) {
  return (u32)(((u64)(uintptr_t)p + GEM_DMA_ADD) >> 32);
}

static u32 gem_mmio_read(u64 addr) { return *(volatile u32*)(uintptr_t)addr; }
static void gem_mmio_write(u64 addr, u32 val) {
  *(volatile u32*)(uintptr_t)addr = val;
  asm volatile("dsb sy" ::: "memory");
}

/* PHY 复位脚是 GPIO32，低有效。不拉一下，MDIO 读出来全是 0xffff。 */
static void phy_reset_gpio(void) {
  u32 pad = gem_mmio_read(PHY_RST_PAD);
  if (pad != 0xdeaddeadu && pad != 0xffffffffu) {
    pad &= ~0x80u;
    pad = (pad & ~0x30u) | 0x10u;
    gem_mmio_write(PHY_RST_PAD, pad);
  }
  gem_mmio_write(PHY_RST_CTRL, 5u | (2u << 12) | (3u << 14));
  gem_udelay(5000);
  gem_mmio_write(PHY_RST_CTRL, 5u | (3u << 12) | (3u << 14));
  gem_udelay(5000);
}

static void gem_rings_init(void) {
  u32 i;
  kmemset(rx_desc, 0, sizeof(rx_desc));
  kmemset(tx_desc, 0, sizeof(tx_desc));
  for (i = 0; i < RX_NUM; i++) {
    rx_desc[i].addr = gem_phys_lo(rx_buf[i]);
    rx_desc[i].addrh = gem_phys_hi(rx_buf[i]);
    rx_desc[i].ctrl = 0;
    if (i + 1 == RX_NUM) rx_desc[i].addr |= RX_WRAP;
  }
  for (i = 0; i < TX_NUM; i++) {
    tx_desc[i].addr = gem_phys_lo(tx_buf[i]);
    tx_desc[i].addrh = gem_phys_hi(tx_buf[i]);
    tx_desc[i].ctrl = TX_USED;
    if (i + 1 == TX_NUM) tx_desc[i].ctrl |= TX_WRAP;
  }
  cpu_flush_dcache_range((unsigned long)rx_desc,
                         (unsigned long)rx_desc + sizeof(rx_desc));
  cpu_flush_dcache_range((unsigned long)tx_desc,
                         (unsigned long)tx_desc + sizeof(tx_desc));
  gem_write(GEM_RBQP, gem_phys_lo(rx_desc));
  gem_write(GEM_TBQP, gem_phys_lo(tx_desc));
  if (gem.dma64) {
    gem_write(GEM_RBQPH, gem_phys_hi(rx_desc));
    gem_write(GEM_TBQPH, gem_phys_hi(tx_desc));
  }
}

/* pcie2 根复合体。窗口和 BAR 写法对齐 raspi5 rp1.c。 */
#define PCIE2_BASE 0x1000120000ULL
#define PCIE_MISC_CTRL 0x4008u
#define PCIE_MEM_WIN0_LO 0x400cu
#define PCIE_MEM_WIN0_HI 0x4010u
#define PCIE_RC_BAR2_LO 0x4034u
#define PCIE_RC_BAR2_HI 0x4038u
#define PCIE_STATUS 0x4068u
#define PCIE_WIN0_BASE_LIMIT 0x4070u
#define PCIE_WIN0_BASE_HI 0x4080u
#define PCIE_WIN0_LIMIT_HI 0x4084u
#define PCIE_UBUS_CTRL 0x40a4u
#define PCIE_UBUS_BAR2_REMAP 0x40b4u
#define PCIE_UBUS_BAR2_REMAP_HI 0x40b8u
#define PCIE_AXI_READ_ERROR 0x4170u
#define PCIE_CFG_DATA 0x8000u
#define PCIE_CFG_INDEX 0x9000u
#define PCIE_LINK_UP ((1u << 4) | (1u << 5))
#define PCI_CMD_MEM_MASTER 0x0006u

static u32 rc_read(u32 off) {
  return *(volatile u32*)(uintptr_t)(PCIE2_BASE + off);
}
static void rc_write(u32 off, u32 val) {
  *(volatile u32*)(uintptr_t)(PCIE2_BASE + off) = val;
  asm volatile("dsb sy" ::: "memory");
}
static void rc_write8(u32 off, u8 val) {
  *(volatile u8*)(uintptr_t)(PCIE2_BASE + off) = val;
  asm volatile("dsb sy" ::: "memory");
}
static void rc_write16(u32 off, u16 val) {
  *(volatile u16*)(uintptr_t)(PCIE2_BASE + off) = val;
  asm volatile("dsb sy" ::: "memory");
}
static void rc_update(u32 off, u32 clear, u32 set) {
  rc_write(off, (rc_read(off) & ~clear) | set);
}
static void cfg_select(void) { rc_write(PCIE_CFG_INDEX, 1u << 20); }
static u32 cfg_read(u32 where) {
  cfg_select();
  return *(volatile u32*)(uintptr_t)(PCIE2_BASE + PCIE_CFG_DATA + where);
}
static void cfg_write(u32 where, u32 val) {
  cfg_select();
  *(volatile u32*)(uintptr_t)(PCIE2_BASE + PCIE_CFG_DATA + where) = val;
  asm volatile("dsb sy" ::: "memory");
}
static void cfg_write16(u32 where, u16 val) {
  cfg_select();
  *(volatile u16*)(uintptr_t)(PCIE2_BASE + PCIE_CFG_DATA + where) = val;
  asm volatile("dsb sy" ::: "memory");
}

/* 64GB 入站窗口的 SIZE 编码：log2(size) - 15。 */
static u32 ibar_size(u64 size) {
  int log2 = 0;
  while ((1ULL << log2) < size) log2++;
  if (log2 >= 16 && log2 <= 37) return (u32)(log2 - 15);
  return 0;
}

/* 对齐 ewokos train_link：BAR2 在放 PERST 之前写，remap 只或上 ACCESS_EN。
 * 不写 remap 的高字。出站窗口仍覆盖到 GEM（PCI +0x100000）。 */
static void pcie_dma_window(void) {
  rc_update(PCIE_MISC_CTRL, 0x00303400u,
            (1u << 12) | (1u << 13) | (1u << 20) | (1u << 10));
  rc_update(PCIE_UBUS_CTRL, 0, (1u << 13) | (1u << 19));
  rc_write(PCIE_AXI_READ_ERROR, 0xffffffffu);
  rc_write(PCIE_MEM_WIN0_LO, 0);
  rc_write(PCIE_MEM_WIN0_HI, 0);
  rc_write(PCIE_WIN0_BASE_LIMIT, 0xfff00000u);
  rc_write(PCIE_WIN0_BASE_HI, 0x1f);
  rc_write(PCIE_WIN0_LIMIT_HI, 0x1f);
  rc_write(PCIE_RC_BAR2_LO, ibar_size(0x1000000000ULL));
  rc_write(PCIE_RC_BAR2_HI, 0x10);
  rc_update(PCIE_UBUS_BAR2_REMAP, 0, 1);
  rc_update(PCIE_MISC_CTRL, 0xf8000000u, 21u << 27);
}

extern void cpu_delay_usec(unsigned long long count);

#define RESCAL_PAGE 0x1000119000ULL
#define RESET_PAGE 0x1001504000ULL

static int pcie_link_up(void) {
  return (rc_read(PCIE_STATUS) & PCIE_LINK_UP) == PCIE_LINK_UP;
}

static int pcie_mdio_write(u8 reg, u16 data) {
  int i;
  rc_write(0x1100, reg);
  (void)rc_read(0x1100);
  rc_write(0x1104, 0x80000000u | data);
  for (i = 0; i < 10; i++) {
    if ((rc_read(0x1104) & 0x80000000u) == 0) return 0;
    cpu_delay_usec(10);
  }
  return -1;
}

/* 链路没起来时按  train_link：rescal、桥复位、PLL，最后再放 PERST。 */
static int pcie_train(void) {
  static const u8 regs[] = {0x16, 0x17, 0x18, 0x19, 0x1b, 0x1c, 0x1e};
  static const u16 data[] = {0x50b9, 0xbda1, 0x0094, 0x97b4, 0x5030, 0x5030, 0x0007};
  u32 v;
  int i;
  u32 lnkcap;
  u16 lnkctl2;

  gem_map(RESCAL_PAGE, 0x1000);
  gem_map(RESET_PAGE, 0x1000);
  v = gem_mmio_read(RESCAL_PAGE + 0x500);
  gem_mmio_write(RESCAL_PAGE + 0x500, v | 1);
  if ((gem_mmio_read(RESCAL_PAGE + 0x500) & 1) == 0) return -1;
  for (i = 0; i < 40 && (gem_mmio_read(RESCAL_PAGE + 0x508) & 1) == 0; i++) {
    cpu_delay_usec(25);
  }
  if ((gem_mmio_read(RESCAL_PAGE + 0x508) & 1) == 0) return -2;
  gem_mmio_write(RESCAL_PAGE + 0x500, gem_mmio_read(RESCAL_PAGE + 0x500) & ~1u);

  gem_mmio_write(RESET_PAGE + 0x330, 1u << 12);
  cpu_delay_usec(100);
  gem_mmio_write(RESET_PAGE + 0x334, 1u << 12);
  cpu_delay_usec(100);
  rc_update(0x4304, 1u << 27, 0);
  cpu_delay_usec(100);

  pcie_mdio_write(0x1f, 0x1600);
  for (i = 0; i < 7; i++) pcie_mdio_write(regs[i], data[i]);
  cpu_delay_usec(100);
  rc_update(0x184c, 0xff, 0x12);

  lnkcap = rc_read(0xac + 0x0c);
  rc_write(0xac + 0x0c, (lnkcap & ~0xfu) | 2);
  lnkctl2 = *(volatile u16*)(uintptr_t)(PCIE2_BASE + 0xac + 0x30);
  *(volatile u16*)(uintptr_t)(PCIE2_BASE + 0xac + 0x30) = (lnkctl2 & ~0xfu) | 2;
  asm volatile("dsb sy" ::: "memory");
  rc_update(0x043c, 0x00ffffffu, 0x00060400u);
  rc_update(0x0188, 0x0cu, 0);
  rc_write(0x40a8, 0x0b2d0000u);
  rc_write(0x405c, 0x0aba0000u);
  rc_write(0x403c, 0);
  pcie_dma_window();
  rc_update(0x4064, 1u << 2, 1u << 2);
  cpu_delay_usec(100000);
  for (i = 0; i < 900 && !pcie_link_up(); i++) cpu_delay_usec(1000);
  return pcie_link_up() ? 0 : -3;
}

static int rp1_pcie_enable(void) {
  u32 id, status;
  gem_map(PCIE2_BASE, 0x10000);
  status = rc_read(PCIE_STATUS);
  if ((status & PCIE_LINK_UP) != PCIE_LINK_UP) {
    int trained = pcie_train();
    status = rc_read(PCIE_STATUS);
    kprintf("gem: pcie2 train %d status %08x\n", trained, status);
    if (trained != 0) return -1;
  }
  pcie_dma_window();
  rc_write8(0x0c, 16);
  rc_write8(0x19, 1);
  rc_write8(0x1a, 1);
  rc_write16(0x20, 0);
  rc_write16(0x22, 0xfff0);
  rc_write16(0x04, PCI_CMD_MEM_MASTER);
  id = cfg_read(0);
  kprintf("gem: rp1 id %08x\n", id);
  if (id == 0xffffffffu || id == 0xdeaddeadu || id == 0) return -1;
  cfg_write(0x10, 4);
  cfg_write(0x14, 0);
  cfg_write(0x18, 0x00400000u | 4u);
  cfg_write(0x1c, 0);
  cfg_write16(0x04, PCI_CMD_MEM_MASTER);
  return 0;
}

static int gem_hw_init(void) {
  u32 id, dcfg6, dma, ncr;
  u32 i;

  if (rp1_pcie_enable() != 0) return -1;
  gem_map(CLK_BASE, 0x1000);
  gem_map(GEM_BASE, GEM_SIZE);
  id = clk_read(CLK_ETH_CTRL);
  if (id == 0xdeaddeadu || id == 0xffffffffu) {
    kprintf("gem: rp1 %lx clk %08x\n", (unsigned long)RP1_CPU_BASE, id);
    return -1;
  }
  gem_clock_on(CLK_ETH_CTRL, CLK_ETH_DIV, CLK_ETH_SEL);
  gem_clock_on(CLK_ETH_TSU_CTRL, CLK_ETH_TSU_DIV, CLK_ETH_TSU_SEL);
  gem_udelay(1000);

  id = gem_read(GEM_MID);
  dcfg6 = gem_read(GEM_DCFG6);
  kprintf("gem: base %lx mid %08x dcfg1 %08x dcfg6 %08x clk %08x\n",
          (unsigned long)GEM_BASE, id, gem_read(GEM_DCFG1), dcfg6,
          clk_read(CLK_ETH_CTRL));
  if (id == 0 || id == 0xffffffffu || id == 0xdeaddeadu) {
    kprintf("gem: controller not present\n");
    return -1;
  }
  if ((dcfg6 & DCFG6_DAW64) == 0) {
    kprintf("gem: dma is not 64-bit\n");
    return -1;
  }
  gem.dma64 = 1;

  gem.mac[0] = 0x02;
  gem.mac[1] = 0x00;
  gem.mac[2] = 0x00;
  gem.mac[3] = 0x00;
  gem.mac[4] = 0x00;
  gem.mac[5] = 0x01;

  gem_write(GEM_NCR, 0);
  gem_write(GEM_IDR, 0xffffffffu);
  gem_write(GEM_NCFGR, (NCFGR_CLK_DIV96 << NCFGR_CLK_SHIFT) | gem_ncfgr_dbw());
  gem_write(GEM_USRIO, USRIO_RGMII);
  ncr = NCR_MPE | NCR_MIIONRGMII;
  gem_write(GEM_NCR, ncr);
  phy_reset_gpio();

  if (phy_find() == 0) {
    mdio_write(gem.phy, MII_BMCR, BMCR_RESET);
    for (i = 0; i < 100; i++) {
      u32 v = 0;
      if (mdio_read(gem.phy, MII_BMCR, &v) != 0) break;
      if ((v & BMCR_RESET) == 0) break;
      gem_udelay(1000);
    }
    mdio_write(gem.phy, MII_ADVERTISE, ADVERTISE_ALL);
    mdio_write(gem.phy, MII_CTRL1000, ADVERTISE_1000FULL | ADVERTISE_1000HALF);
    mdio_write(gem.phy, MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART);
    for (i = 0; i < 40; i++) {
      phy_poll();
      if (gem.link_up) break;
      gem_udelay(20000);
    }
  }
  kprintf("gem: mac %02x:%02x:%02x:%02x:%02x:%02x link %u speed %u\n",
          gem.mac[0], gem.mac[1], gem.mac[2], gem.mac[3], gem.mac[4], gem.mac[5],
          gem.link_up, gem.speed);

  gem_write(GEM_SA1B, gem.mac[0] | ((u32)gem.mac[1] << 8) |
                          ((u32)gem.mac[2] << 16) | ((u32)gem.mac[3] << 24));
  gem_write(GEM_SA1T, gem.mac[4] | ((u32)gem.mac[5] << 8));
  gem_apply_link();
  dma = DMACFG_FBLDO_16 | DMACFG_RXBMS_FULL | DMACFG_TXPBMS |
        ((GEM_MTU / 64) << DMACFG_RXBS_SHIFT);
  if (gem.dma64) dma |= DMACFG_ADDR64;
  gem_write(GEM_DMACFG, dma);
  gem_rings_init();
  gem_write(GEM_RSR, 0xffffffffu);
  gem_write(GEM_TSR, 0xffffffffu);
  gem_write(GEM_NCR, ncr | NCR_RE | NCR_TE);
  if (gem.dma64) {
    gem_write(GEM_RBQPH, gem_phys_hi(rx_desc));
    gem_write(GEM_TBQPH, gem_phys_hi(tx_desc));
  }
  kprintf("gem: dma %08x%08x bar2 %08x %08x remap %08x q %08x %08x\n",
          gem_phys_hi(tx_desc), gem_phys_lo(tx_desc), rc_read(PCIE_RC_BAR2_LO),
          rc_read(PCIE_RC_BAR2_HI), rc_read(PCIE_UBUS_BAR2_REMAP),
          gem_read(GEM_TBQP), gem_read(GEM_TBQPH));
  gem.ready = 1;
  return 0;
}

static int gem_rx_find(gem_desc_t* d, u32* flen) {
  u32 w[3];
  u32 i;
  w[0] = d->ctrl;
  w[1] = d->addrh;
  w[2] = d->rsvd;
  for (i = 0; i < 3; i++) {
    if ((w[i] & RX_SOF) != 0 && (w[i] & RX_EOF) != 0) {
      *flen = w[i] & 0x1fffu;
      if (*flen >= 14 && *flen <= GEM_MTU) return 1;
    }
  }
  return 0;
}

static size_t gem_net_read(device_t* dev, void* buf, size_t len) {
  u32 n;
  static u32 rx_logged;
  (void)dev;
  if (!gem.ready || buf == NULL || len == 0) return 0;
  phy_poll();
  if (!gem.link_up) return 0;
  /* REC/BNA 是写 1 清除。不清的话环一满就不再收。 */
  gem_write(GEM_RSR, 0xffffffffu);
  for (n = 0; n < RX_NUM; n++) {
    u32 idx = gem.rx_next;
    gem_desc_t* d = &rx_desc[idx];
    u32 addr, flen;
    cpu_invalidate_dcache_range((unsigned long)d, (unsigned long)d + sizeof(*d));
    addr = d->addr;
    if ((addr & RX_USED) == 0) return 0;
    if (!gem_rx_find(d, &flen)) {
      gem_udelay(20);
      cpu_invalidate_dcache_range((unsigned long)d, (unsigned long)d + sizeof(*d));
      addr = d->addr;
    }
    gem.rx_next = (idx + 1) % RX_NUM;
    if (gem_rx_find(d, &flen)) {
      cpu_invalidate_dcache_range((unsigned long)rx_buf[idx],
                                  (unsigned long)rx_buf[idx] + flen);
      if (flen > len) flen = (u32)len;
      kmemcpy(buf, rx_buf[idx], flen);
      gem.rx_frames++;
      d->addr = addr & ~RX_USED;
      cpu_flush_dcache_range((unsigned long)d, (unsigned long)d + sizeof(*d));
      return flen;
    }
    if (!rx_logged) {
      rx_logged = 1;
      kprintf("gem: rx drop %08x %08x %08x %08x\n", d->addr, d->ctrl, d->addrh,
              d->rsvd);
    }
    gem.rx_errors++;
    d->addr = addr & ~RX_USED;
    cpu_flush_dcache_range((unsigned long)d, (unsigned long)d + sizeof(*d));
  }
  return 0;
}

static size_t gem_net_write(device_t* dev, const void* buf, size_t len) {
  gem_desc_t* d;
  u32 ctrl, tlen, n;
  u8* dst;
  (void)dev;
  if (!gem.ready || buf == NULL || len == 0) return 0;
  phy_poll();
  if (!gem.link_up) return 0;
  if (len > GEM_MTU) len = GEM_MTU;
  d = &tx_desc[gem.tx_next];
  cpu_invalidate_dcache_range((unsigned long)d, (unsigned long)d + sizeof(*d));
  if ((d->ctrl & TX_USED) == 0) {
    u32 spin;
    for (spin = 0; spin < 500 && (d->ctrl & TX_USED) == 0; spin++) {
      gem_udelay(20);
      cpu_invalidate_dcache_range((unsigned long)d, (unsigned long)d + sizeof(*d));
    }
  }
  if ((d->ctrl & TX_USED) == 0) {
    gem.tx_busy++;
    if (gem.tx_busy == 1) {
      kprintf("gem: tx stall tsr %08x rsr %08x ncr %08x cfg %08x dma %08x desc %08x\n",
              gem_read(GEM_TSR), gem_read(GEM_RSR), gem_read(GEM_NCR),
              gem_read(GEM_NCFGR), gem_read(GEM_DMACFG), d->ctrl);
    }
    return 0;
  }
  n = gem.tx_next;
  dst = tx_buf[n];
  tlen = len < 60 ? 60 : (u32)len;
  kmemcpy(dst, buf, len);
  if (tlen > len) kmemset(dst + len, 0, tlen - len);
  cpu_flush_dcache_range((unsigned long)dst, (unsigned long)dst + tlen);
  ctrl = (tlen & 0x3fffu) | TX_LAST;
  if (n + 1 == TX_NUM) ctrl |= TX_WRAP;
  d->ctrl = ctrl;
  cpu_flush_dcache_range((unsigned long)d, (unsigned long)d + sizeof(*d));
  gem.tx_next = (n + 1) % TX_NUM;
  gem_write(GEM_NCR, gem_read(GEM_NCR) | NCR_TSTART);
  gem.tx_frames++;
  return len;
}

static int gem_net_ioctl(device_t* dev, u32 cmd, void* args) {
  (void)dev;
  switch (cmd) {
    case 0x01:
      if (args) kmemcpy(args, gem.mac, 6);
      return 0;
    case 0x02:
      phy_poll();
      if (args) *(u32*)args = gem.link_up;
      return gem.link_up ? 1 : 0;
    case 0x03:
      if (args) *(u32*)args = (gem.speed & 0xffff) | ((gem.duplex & 0xffff) << 16);
      return 0;
    case 0x04:
      if (args) *(u32*)args = gem.phy_id;
      return 0;
    case 0x05:
      if (args) {
        u32* p = (u32*)args;
        p[0] = gem.rx_frames;
        p[1] = gem.rx_errors;
        p[2] = gem.tx_frames;
        p[3] = gem.tx_errors;
        p[4] = gem.tx_busy;
      }
      return 0;
    default:
      return -1;
  }
}

int net_init_device(device_t* dev) {
  kmemset(&gem, 0, sizeof(gem));
  if (gem_hw_init() != 0) {
    dev->read = gem_net_read;
    dev->write = gem_net_write;
    dev->ioctl = gem_net_ioctl;
    dev->data = &gem;
    return -1;
  }
  dev->read = gem_net_read;
  dev->write = gem_net_write;
  dev->ioctl = gem_net_ioctl;
  dev->data = &gem;
  kprintf("gem: rp1 ethernet ready\n");
  return 0;
}
