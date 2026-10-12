/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
/* Raspberry Pi 5 (BCM2712) 风扇模块（CPU 温度见 duck/modules/temp）。
 *
 * 【硬件背景】Pi5 的官方风扇（J12，JST-SH 4P）挂在 RP1 南桥，不在 SoC：
 *   PWM  = RP1_PWM1 通道 3，~24kHz，极性反转（CTRL.POLARITY=1）
 *   引脚 = RP1 GPIO45，funcsel 0（pwm1），bias-pull-down
 *   RPM  = RP1 的 M-core 固件数 TACH 脉冲，发布在通道 2 的 PHASE 寄存器
 *
 * 【RP1 窗口用 0x1F00000000，不是 0x1C00000000】
 *   出站窗口 CPU 0x1F00000000 -> PCIe 地址 0（MEM_WIN0_BASE_HI=0x1f），
 *   RP1 寄存器文件挂在 PCIe 0x40000000（BAR2），因此 0x1C 窗口上的
 *   GPIO 探针从未真正落在 RP1 上（init.c "纠正历史错误" 段有记录）。
 *   platform_map() 已把 0x1F 窗口前 1MB 恒等映射为 PAGE_DEV，本模块直接访问。
 *
 * 【安全闸：RP1 窗口只在 PCIe2 链路 up 后才解码】对未解码的 PCIe 地址做读
 *   会拿 UR -> 同步外部中止 -> 内核死（bcm2835.h 老坑同款）。所以 init 时
 *   先读根复合体 PCIE_STATUS（SoC 内部寄存器，恒解码、安全）确认
 *   DL_ACTIVE|PHYLINKUP；链路没起来就跳过初始化、/dev/fan 禁用并告警。
 *   固件（EEPROM bootloader）正常已把链路训练好；若真遇到没起来的机器，
 ********************************************************************/
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "kernel/page.h"
#include "dev/devfs.h"

/* ---- RP1 窗口内的块偏移 ------------------------------------------- */
#define RP1_BASE            0x1F00000000ULL
#define RP1_PWM1_OFF        0x9C000
#define RP1_CLOCKS_OFF      0x18000
#define RP1_IO_BANK_OFF     0xD0000
#define RP1_PADS_BANK_OFF   0xF0000

/* PWM1（偏移取自 pwm-rp1.c） */
#define PWM_GLB_CTRL        0x000
#define PWM_GLB_CTRL_EN(ch) (1u << (ch))
#define PWM_GLB_CTRL_UPDATE (1u << 31)
#define PWM_CHAN_CTRL(ch)   (0x014 + (ch) * 0x10)
#define PWM_CTRL_MODE_TE_MS 0x1        /* trailing edge, M/S modulation */
#define PWM_CTRL_POLARITY   (1u << 3)  /* 反相：风扇线 duty0%=全速 */
#define PWM_CTRL_FIFO_POP   (1u << 8)
#define PWM_RANGE(ch)       (0x018 + (ch) * 0x10)
#define PWM_PHASE(ch)       (0x01C + (ch) * 0x10)
#define PWM_DUTY(ch)        (0x020 + (ch) * 0x10)
#define PWM_RPM_REG         (PWM_PHASE(2)) /* M-core 固件发布的转速 */

/* RP1_CLK_PWM1（clocks@18000，xosc=50MHz unity divider） */
#define CLK_PWM1_CTRL       0x084
#define CLK_PWM1_DIV_INT    0x088
#define CLK_PWM1_DIV_FRAC   0x08C
#define CLK_CTRL_ENABLE     (1u << 11)
#define CLK_CTRL_AUXSRC(x)  ((x) << 5)
#define CLK_CTRL_SRC_AUX    0x1
#define CLK_SRC_MASK        0x1F
#define CLK_PWM1_SRC_XOSC   2

#define FAN_PWM_CHAN        3
#define FAN_PWM_GPIO        45
#define FAN_PWM_CLK_HZ      50000000
#define FAN_PWM_PERIOD_NS   41566 /* ~24 kHz */
#define FAN_PWM_RANGE_TICKS \
  ((u32)(((u64)FAN_PWM_CLK_HZ * FAN_PWM_PERIOD_NS) / 1000000000ULL))

#define FAN_LEVEL_MAX       10
#define FAN_LEVEL_DEFAULT   5

/* GPIO45 寄存器地址（RP1 三段式布局：每区 3 个 bank，步进 0x4000；
 * bank2 = GPIO34-53。IO_BANK 每脚 8 字节、CTRL 在 +4；PADS 每脚 4 字节） */
#define GPIO_BANK(pin)      ((pin) < 28 ? 0 : ((pin) < 34 ? 1 : 2))
#define GPIO_BANK_PIN(pin)  ((pin) < 28 ? (pin) : ((pin) < 34 ? (pin)-28 : (pin)-34))

/* PADS 位域：bit7 OUT_DISABLE、bit6 IN_ENABLE、bit3:2 PULL（01=下拉） */
#define PAD_OUT_DISABLE     0x80u
#define PAD_IN_ENABLE       0x40u
#define PAD_PULL_MASK       0x0Cu
#define PAD_PULL_DOWN       (1u << 2)

/* 根复合体（SoC 内部，恒解码）。偏移取自 Linux pcie-brcmstb.c，
 * 与 platform/raspi5/init.c 诊断代码一致 */
#define RC_BASE             0x1000120000ULL
#define RC_PCIE_STATUS      0x4068u
#define PCIE_STATUS_PHY_LINK_UP (1u << 4)
#define PCIE_STATUS_DL_ACTIVE   (1u << 5)

/* firmware mailbox 温度见独立模块 duck/modules/cpu（/dev/cpu） */
static int fan_ready;          /* RP1 链路确认 + PWM 已初始化 */
static int fan_level = FAN_LEVEL_DEFAULT;

static inline u32 rp1_rd(u32 off) {
  return *(volatile u32*)(uintptr_t)(RP1_BASE + off);
}
static inline void rp1_wr(u32 off, u32 v) {
  *(volatile u32*)(uintptr_t)(RP1_BASE + off) = v;
  dsb();
}

/* ---- 风扇 PWM ------------------------------------------------------ */

static u32 fan_level_to_duty_ticks(int level) {
  u32 pct = (u32)(level * 100 / FAN_LEVEL_MAX);
  return (u32)(((u64)FAN_PWM_RANGE_TICKS * pct) / 100);
}

/* level 0 = gate off 通道（官方 pwm-rp1 的 disable 路径，与极性无关） */
static void fan_commit(void) {
  u32 glb = rp1_rd(RP1_PWM1_OFF + PWM_GLB_CTRL);
  if (fan_level == 0) {
    glb &= ~PWM_GLB_CTRL_EN(FAN_PWM_CHAN);
  } else {
    glb |= PWM_GLB_CTRL_EN(FAN_PWM_CHAN);
  }
  rp1_wr(RP1_PWM1_OFF + PWM_GLB_CTRL, glb | PWM_GLB_CTRL_UPDATE);
}

static void fan_set_level(int level) {
  if (level < 0) level = 0;
  if (level > FAN_LEVEL_MAX) level = FAN_LEVEL_MAX;
  fan_level = level;
  if (!fan_ready) return;
  rp1_wr(RP1_PWM1_OFF + PWM_DUTY(FAN_PWM_CHAN),
         fan_level_to_duty_ticks(fan_level));
  fan_commit();
}

/* bootloader 若已开时钟就保留其配置，否则手动使能（xosc 50MHz unity）。
 * 时钟没开时 PWM 核不出波形，寄存器写入全被风扇无视 */
static void fan_clk_init(void) {
  u32 ctrl = rp1_rd(RP1_CLOCKS_OFF + CLK_PWM1_CTRL);
  if (ctrl & CLK_CTRL_ENABLE) return;
  rp1_wr(RP1_CLOCKS_OFF + CLK_PWM1_DIV_INT, 1);
  rp1_wr(RP1_CLOCKS_OFF + CLK_PWM1_DIV_FRAC, 0);
  ctrl &= ~(CLK_CTRL_AUXSRC(0x1F) | CLK_SRC_MASK);
  ctrl |= CLK_CTRL_SRC_AUX | CLK_CTRL_AUXSRC(CLK_PWM1_SRC_XOSC);
  rp1_wr(RP1_CLOCKS_OFF + CLK_PWM1_CTRL, ctrl);
  rp1_wr(RP1_CLOCKS_OFF + CLK_PWM1_CTRL, ctrl | CLK_CTRL_ENABLE);
}

/* GPIO45 -> PWM1 (funcsel 0) + 下拉。RP1 的方向/复用语义见
 * gpio.c：先把 pad 放行（IN_ENABLE、清 OUT_DISABLE），再写 CTRL 的
 * FUNCSEL，OUTOVER/OEOVER 取 PERI（0）。 */
static void fan_gpio_init(void) {
  u32 bank = GPIO_BANK(FAN_PWM_GPIO);
  u32 bpin = GPIO_BANK_PIN(FAN_PWM_GPIO);
  u32 ctrl_off = RP1_IO_BANK_OFF + bank * 0x4000 + bpin * 8 + 0x04;
  u32 pad_off = RP1_PADS_BANK_OFF + bank * 0x4000 + 0x04 + bpin * 4;

  u32 pad = rp1_rd(pad_off);
  pad &= ~(PAD_OUT_DISABLE | PAD_PULL_MASK);
  pad |= PAD_IN_ENABLE | PAD_PULL_DOWN;
  rp1_wr(pad_off, pad);

  u32 ctrl = rp1_rd(ctrl_off);
  ctrl &= ~0x1Fu; /* 清 FUNCSEL，OUTOVER/OEOVER 本来就是 0(PERI) */
  rp1_wr(ctrl_off, ctrl);
}

static void fan_hw_init(void) {
  fan_clk_init();
  fan_gpio_init();

  /* 整通道重初始化，不信任 bootloader 残留 */
  rp1_wr(RP1_PWM1_OFF + PWM_RANGE(FAN_PWM_CHAN), FAN_PWM_RANGE_TICKS);
  rp1_wr(RP1_PWM1_OFF + PWM_DUTY(FAN_PWM_CHAN), FAN_PWM_RANGE_TICKS);
  rp1_wr(RP1_PWM1_OFF + PWM_CHAN_CTRL(FAN_PWM_CHAN),
         PWM_CTRL_FIFO_POP | PWM_CTRL_POLARITY | PWM_CTRL_MODE_TE_MS);
  fan_commit();
}

/* ---- PCIe2 链路训练（SD 卡启动时 bootloader 不训练 PCIe2） -------------
 * 序列照抄 ewokos arch_bcm2712/src/rp1.c 的 train_link()/bcm2712_rp1_init()
 * （源自 Linux pcie-brcmstb.c 与 Circle），只保留 RP1 出寄存器窗口必需的
 * 步骤；省掉 tc_qos/ASPM 锁定（那些是 ewokos 为显示扫描率做的，风扇无关）。
 * RC/RESCAL/RESET 三处地址都由 platform_map() 预映射（见 init.c）。 */
#define RC_PCIE_CTRL              0x4064u
#define RC_MISC_CTRL              0x4008u
#define RC_MEM_WIN0_LO            0x400cu
#define RC_MEM_WIN0_HI            0x4010u
#define RC_MEM_WIN0_BASE_LIMIT    0x4070u
#define RC_MEM_WIN0_BASE_HI       0x4080u
#define RC_MEM_WIN0_LIMIT_HI      0x4084u
#define RC_BAR1_CONFIG_LO         0x402cu
#define RC_BAR1_CONFIG_HI         0x4030u
#define RC_BAR2_CONFIG_LO         0x4034u
#define RC_BAR2_CONFIG_HI         0x4038u
#define RC_BAR3_CONFIG_LO         0x403cu
#define RC_CONFIG_RETRY_TIMEOUT   0x405cu
#define RC_UBUS_CTRL              0x40a4u
#define RC_UBUS_TIMEOUT           0x40a8u
#define RC_UBUS_BAR1_CONFIG_REMAP 0x40acu
#define RC_UBUS_BAR1_CONFIG_HI    0x40b0u
#define RC_UBUS_BAR2_CONFIG_REMAP 0x40b4u
#define RC_AXI_READ_ERROR_DATA    0x4170u
#define RC_HARD_PCIE_HARD_DEBUG   0x4304u
#define RC_PL_PHY_CTL_15          0x184cu
#define RC_DL_MDIO_ADDR           0x1100u
#define RC_DL_MDIO_WR_DATA        0x1104u
#define RC_VENDOR_VS_REG1         0x0188u
#define RC_PRIV1_ID_VAL3          0x043cu
#define RC_CAP_REGS               0x00acu /* BRCM 扩展 cap 基址 */
#define PCIE_EXT_CFG_INDEX        0x9000u
#define PCIE_EXT_CFG_DATA         0x8000u

#define RESCAL_BASE               0x1000119000ULL
#define RESET_BASE                0x1001504000ULL

/* PCI 配置空间公共偏移 */
#define PCI_CMD 0x04
#define PCI_CACHE 0x0c
#define PCI_SEC_BUS 0x19
#define PCI_SUB_BUS 0x1a
#define PCI_MEM_BASE 0x20
#define PCI_MEM_LIMIT 0x22
#define PCI_BRIDGE_CTL 0x3e
#define PCI_BAR0 0x10
#define PCI_BAR2 0x18
#define PCI_CMD_MEM 0x0002u
#define PCI_CMD_MASTER 0x0004u
#define PCI_CMD_PARITY 0x0040u
#define PCI_CMD_SERR 0x0100u
#define PCI_BAR_MEM_64 0x00000004u

/* 内核态微秒延时：通用定时器（恒可用，不依赖任何未映射外设）。
 * cntfrq=54MHz，100ms = 5.4e6 tick，u64 乘法无溢出。 */
static void fan_delay_us(u64 us) {
  u64 freq, start, now;
  asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
  asm volatile("mrs %0, cntvct_el0" : "=r"(start));
  u64 end = start + (us * freq) / 1000000ULL;
  do {
    asm volatile("mrs %0, cntvct_el0" : "=r"(now));
  } while (now < end);
}

static inline u32 rc_rd(u32 off) {
  return *(volatile u32*)(uintptr_t)(RC_BASE + off);
}
static inline void rc_wr(u32 off, u32 v) {
  *(volatile u32*)(uintptr_t)(RC_BASE + off) = v;
  dsb();
}
static void rc_update(u32 off, u32 clr, u32 set) {
  rc_wr(off, (rc_rd(off) & ~clr) | set);
}
static inline u8 rc_rd8(u32 off) {
  return *(volatile u8*)(uintptr_t)(RC_BASE + off);
}
static inline void rc_wr8(u32 off, u8 v) {
  *(volatile u8*)(uintptr_t)(RC_BASE + off) = v;
}
static inline u16 rc_rd16(u32 off) {
  return *(volatile u16*)(uintptr_t)(RC_BASE + off);
}
static inline void rc_wr16(u32 off, u16 v) {
  *(volatile u16*)(uintptr_t)(RC_BASE + off) = v;
}

/* BAR 尺寸编码（brcmstb ibar）：12-15 -> 0x1c..0x1f，16-37 -> 0..21 */
static u32 pcie_encode_ibar_size(u64 size) {
  int log2 = 0;
  while ((1ULL << log2) < size) log2++;
  if (log2 >= 12 && log2 <= 15) return (u32)(log2 - 12 + 0x1c);
  if (log2 >= 16 && log2 <= 37) return (u32)(log2 - 15);
  return 0;
}

/* SerDes PLL 参数（Linux brcm_pcie MDIO munge，序列 ewokos 同源） */
static int pcie_mdio_write(u8 reg, u16 data) {
  rc_wr(RC_DL_MDIO_ADDR, reg);
  (void)rc_rd(RC_DL_MDIO_ADDR);
  rc_wr(RC_DL_MDIO_WR_DATA, 0x80000000u | data);
  for (int i = 0; i < 10; i++) {
    if (!(rc_rd(RC_DL_MDIO_WR_DATA) & 0x80000000u)) return 0;
    fan_delay_us(10);
  }
  return -1;
}

static int pcie_munge_pll(void) {
  static const u8 regs[] = {0x16, 0x17, 0x18, 0x19, 0x1b, 0x1c, 0x1e};
  static const u16 data[] = {0x50b9, 0xbda1, 0x0094, 0x97b4,
                             0x5030, 0x5030, 0x0007};
  int ret = pcie_mdio_write(0x1f, 0x1600);
  for (unsigned i = 0; i < sizeof(regs); i++) {
    ret |= pcie_mdio_write(regs[i], data[i]);
  }
  fan_delay_us(100);
  return ret;
}

static int pcie_link_up(void) {
  u32 status = rc_rd(RC_PCIE_STATUS);
  return (status & (PCIE_STATUS_PHY_LINK_UP | PCIE_STATUS_DL_ACTIVE)) ==
         (PCIE_STATUS_PHY_LINK_UP | PCIE_STATUS_DL_ACTIVE);
}

static int pcie_train_link(void) {
  /* RESCAL deassert（Circle rescal_reset_deassert 同款时序） */
  u32 value = *(volatile u32*)(uintptr_t)(RESCAL_BASE + 0x500);
  *(volatile u32*)(uintptr_t)(RESCAL_BASE + 0x500) = value | 1;
  dsb();
  if (!(*(volatile u32*)(uintptr_t)(RESCAL_BASE + 0x500) & 1)) return -1;
  for (int i = 0; i < 40; i++) {
    if (*(volatile u32*)(uintptr_t)(RESCAL_BASE + 0x508) & 1) break;
    fan_delay_us(25);
  }
  if (!(*(volatile u32*)(uintptr_t)(RESCAL_BASE + 0x508) & 1)) return -2;
  *(volatile u32*)(uintptr_t)(RESCAL_BASE + 0x500) =
      *(volatile u32*)(uintptr_t)(RESCAL_BASE + 0x500) & ~1u;

  /* reset 44（bank1 bit12）assert -> deassert */
  *(volatile u32*)(uintptr_t)(RESET_BASE + 0x318 + 0x18) = 1u << 12;
  dsb();
  fan_delay_us(100);
  *(volatile u32*)(uintptr_t)(RESET_BASE + 0x318 + 0x18 + 4) = 1u << 12;
  dsb();
  fan_delay_us(100);
  rc_update(RC_HARD_PCIE_HARD_DEBUG, 1u << 27, 0);
  fan_delay_us(100);

  if (pcie_munge_pll() != 0) return -4;
  rc_update(RC_PL_PHY_CTL_15, 0xff, 0x12);
  rc_update(RC_MISC_CTRL, 0x00303400u,
            (1u << 12) | (1u << 13) | (1u << 20) | (1u << 10));

  /* 64GB 入站 DMA 窗口 @ PCIe 0x10_00000000 */
  rc_wr(RC_BAR2_CONFIG_LO, pcie_encode_ibar_size(0x1000000000ULL));
  rc_wr(RC_BAR2_CONFIG_HI, 0x10);
  rc_update(RC_UBUS_BAR2_CONFIG_REMAP, 0, 1);
  rc_update(RC_MISC_CTRL, 0xf8000000u, 21u << 27);
  rc_update(RC_UBUS_CTRL, 0, (1u << 13) | (1u << 19));
  rc_wr(RC_AXI_READ_ERROR_DATA, 0xffffffffu);
  rc_wr(RC_UBUS_TIMEOUT, 0x0b2d0000u);
  rc_wr(RC_CONFIG_RETRY_TIMEOUT, 0x0aba0000u);
  rc_wr(RC_BAR3_CONFIG_LO, 0);

  /* Gen2、bridge class、出站窗口 CPU 0x1F00000000 -> PCIe 0 */
  u32 lnkcap = rc_rd(RC_CAP_REGS + 0x0c); /* LNKCAP */
  rc_wr(RC_CAP_REGS + 0x0c, (lnkcap & ~0xfu) | 2);
  u16 lnkctl2 = rc_rd16(RC_CAP_REGS + 0x30); /* LNKCTL2 */
  rc_wr16(RC_CAP_REGS + 0x30, (lnkctl2 & ~0xfu) | 2);
  rc_update(RC_PRIV1_ID_VAL3, 0x00ffffffu, 0x00060400u);
  rc_wr(RC_MEM_WIN0_LO, 0);
  rc_wr(RC_MEM_WIN0_HI, 0);
  rc_wr(RC_MEM_WIN0_BASE_LIMIT, 0xfff00000u);
  rc_wr(RC_MEM_WIN0_BASE_HI, 0x1f);
  rc_wr(RC_MEM_WIN0_LIMIT_HI, 0x1f);
  rc_update(RC_VENDOR_VS_REG1, 0x0c, 0);

  /* MIP 映射（BAR1） */
  rc_wr(RC_BAR1_CONFIG_LO, 0xfffff000u | pcie_encode_ibar_size(0x1000));
  rc_wr(RC_BAR1_CONFIG_HI, 0xff);
  rc_wr(RC_UBUS_BAR1_CONFIG_REMAP, 0x00130000u | 1);
  rc_wr(RC_UBUS_BAR1_CONFIG_HI, 0x10);

  /* PERST# deassert，等链路最多 1 秒 */
  rc_update(RC_PCIE_CTRL, 1u << 2, 1u << 2);
  fan_delay_us(100000);
  for (int w = 0; w < 900 && !pcie_link_up(); w++) {
    fan_delay_us(1000);
  }
  return pcie_link_up() ? 0 : -3;
}

/* 链路起来后使能 bridge 转发 + RP1 endpoint 访问（ewokos rp1_init 同款，
 * 省掉 QoS/ASPM 部分）。返回 0 = RP1 配置空间可访问。 */
static int pcie_enumerate_rp1(void) {
  rc_wr8(PCI_CACHE, 64 / 4);
  rc_wr8(PCI_SEC_BUS, 1);
  rc_wr8(PCI_SUB_BUS, 1);
  rc_wr16(PCI_MEM_BASE, 0);
  rc_wr16(PCI_MEM_LIMIT, 0);
  rc_wr16(PCI_BRIDGE_CTL, 0x0001u); /* parity */
  rc_wr16(PCI_CMD, PCI_CMD_MEM | PCI_CMD_MASTER | PCI_CMD_PARITY |
                       PCI_CMD_SERR);

  /* RP1 endpoint 在 bus1/dev0：读 vendor id 确认在位 */
  rc_wr(PCIE_EXT_CFG_INDEX, 1u << 20);
  u32 id = rc_rd(PCIE_EXT_CFG_DATA);
  if (id == 0xffffffffu || id == 0xdeaddeadu) return -1;

  u32 cfg = PCIE_EXT_CFG_DATA;
  rc_wr8(cfg + PCI_CACHE, 64 / 4);
  rc_wr(cfg + PCI_BAR0, PCI_BAR_MEM_64);
  rc_wr(cfg + PCI_BAR0 + 4, 0);
  rc_wr(cfg + PCI_BAR2, 0x00400000u | PCI_BAR_MEM_64);
  rc_wr(cfg + PCI_BAR2 + 4, 0);
  rc_wr16(cfg + PCI_CMD, PCI_CMD_MEM | PCI_CMD_MASTER | PCI_CMD_PARITY |
                             PCI_CMD_SERR);
  kprintf("fan: rp1 endpoint id=%08x enabled\n", id);
  return 0;
}

/* 根复合体链路状态。RC 寄存器窗口（0x1000120000 起 10 页）由 platform_map()
 * 预映射【不能在这里自己 page_map】真机实测：模块运行期对 64GB 区间的
 * "新 L1 项"做 page_map 会建表失败，访问时 translation fault L3 直接打死
 * 内核线程（详见 platform/raspi5/init.c 的注释）。 */
static int rp1_link_up(void) {
  u32 status = rc_rd(RC_PCIE_STATUS);
  return (status & (PCIE_STATUS_PHY_LINK_UP | PCIE_STATUS_DL_ACTIVE)) ==
         (PCIE_STATUS_PHY_LINK_UP | PCIE_STATUS_DL_ACTIVE);
}

/* ---- 设备接口 ------------------------------------------------------- */

/* /dev/fan：write 1 字节 = level(0-10)；read 1 字节 = 当前 level */
static size_t fan_read(device_t* dev, void* buf, size_t len) {
  if (len < 1) return 0;
  ((u8*)buf)[0] = (u8)fan_level;
  return 1;
}

static size_t fan_write(device_t* dev, const void* buf, size_t len) {
  if (len < 1) return 0;
  fan_set_level(((const u8*)buf)[0]);
  return 1;
}

int fan_init(void) {
  int up = rp1_link_up();
  if (!up) {
    /* SD 卡启动的 bootloader 不训练 PCIe2（只有 USB/NVMe 启动才需要），
     * 自己走一遍 ewokos 同款训练序列 */
    kprintf("fan: pcie2 link down, training...\n");
    int r = pcie_train_link();
    if (r != 0) {
      kprintf("fan: pcie2 train failed step=%d\n", r);
    } else {
      kprintf("fan: pcie2 link trained\n");
      if (pcie_enumerate_rp1() != 0) {
        kprintf("fan: rp1 endpoint not found\n");
      }
      up = rp1_link_up();
    }
  }
  if (up) {
    fan_hw_init();
    fan_set_level(FAN_LEVEL_DEFAULT);
    fan_ready = 1;
    kprintf("fan: RP1 PWM1 ch%d %d Hz inverted, level %d/%d rpm %d\n",
            FAN_PWM_CHAN, 1000000000 / FAN_PWM_PERIOD_NS, fan_level,
            FAN_LEVEL_MAX, (int)rp1_rd(RP1_PWM1_OFF + PWM_RPM_REG));
  } else {
    kprintf("fan: pcie2 link down, /dev/fan disabled\n");
  }

  device_t* fan_dev = kmalloc(sizeof(device_t), DEFAULT_TYPE);
  fan_dev->name = "fan";
  fan_dev->read = fan_read;
  fan_dev->write = fan_write;
  fan_dev->id = DEVICE_FAN;
  fan_dev->type = DEVICE_TYPE_CHAR;
  device_add(fan_dev);

  vnode_t* vnode = vfs_create_node("fan", V_FILE);
  vfs_mount(NULL, "/dev", vnode);
  vnode->device = device_find(DEVICE_FAN);
  vnode->op = &device_operator;

  return 0;
}

void fan_exit(void) { fan_set_level(0); }

module_t fan_module = {.name = "fan", .init = fan_init, .exit = fan_exit};
