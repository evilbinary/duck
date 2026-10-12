/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
/* Raspberry Pi 5 (BCM2712 / RP1) GPIO 平台实现。
 *
 * 【与 BCM283x 完全不同】RP1 没有 FSEL/SET/CLR/LEV 寄存器文件，而是三个
 * 独立区域（布局取自官方 DT rp1.dtsi / linux pinctrl-rp1.c，与 ewokos
 * arch_bcm2712/src/gpio.c 同源）：
 *   IO_BANK  +0xD0000  每脚两字（STATUS + CTRL：funcsel/OEOVER/OUTOVER）
 *   SYS_RIO  +0xE0000  out/oe/in 位图，每区域三个 bank、步进 0x4000
 *   PADS_BANK +0xF0000 每脚一字（pull/drive/in_enable/out_disable）
 * bank 划分：0=GPIO0-27（40pin 排针）、1=28-33（以太网/USB 控制）、
 * 2=34-53（内部）。
 *
 * 【pin 用全局 GPIO 号 0-53】RP1 是单一控制器，gpio_config(gpio, pin, val)
 * 的 gpio 参数忽略 —— 与 sunxi 系的"bank + 组内 pin"编号不同，调用方注意。
 *
 * 【寄存器别名】每区域每个寄存器四份：RW/XOR/SET/CLR（+0/+0x1000/0x2000/
 * 0x3000），用 SET/CLR 原子置位清位，避免 BCM283x 那类读改写竞态。
 *
 * 【映射】三个区域都在 RP1 窗口 0x1F00000000 的前 1MB 内，platform_map()
 * 已按 Device 映射 —— 【勿在模块运行期自己 page_map】真机实测运行期对
 * 64GB 区间的新 L1 项建表失败会 translation fault（见 platform/raspi5/
 * init.c 注释）。
 ********************************************************************/
#include "gpio.h"
#include "kernel/kernel.h"

#define RP1_BASE          0x1F00000000ULL
#define RP1_IO_BANK_OFF   0xD0000
#define RP1_SYS_RIO_OFF   0xE0000
#define RP1_PADS_BANK_OFF 0xF0000

#define RP1_BANK_STRIDE   0x4000
#define RP1_NUM_GPIOS     54

#define RP1_RW_OFFSET     0x0000
#define RP1_XOR_OFFSET    0x1000
#define RP1_SET_OFFSET    0x2000
#define RP1_CLR_OFFSET    0x3000

/* IO_BANK：每脚两字 */
#define RP1_GPIO_CTRL       0x04
#define RP1_GPIO_PIN_STRIDE 0x08
#define RP1_CTRL_FUNCSEL_LSB  0
#define RP1_CTRL_FUNCSEL_MASK 0x1Fu
#define RP1_CTRL_OUTOVER_LSB  12
#define RP1_CTRL_OUTOVER_MASK 0x3000u
#define RP1_CTRL_OEOVER_LSB   14
#define RP1_CTRL_OEOVER_MASK  0xC000u
#define RP1_OUTOVER_PERI    0
#define RP1_OEOVER_PERI     0
#define RP1_OEOVER_DISABLE  2

/* SYS_RIO：bank 内每脚一位 */
#define RP1_RIO_OUT 0x00
#define RP1_RIO_OE  0x04
#define RP1_RIO_IN  0x08

/* PADS_BANK：每脚一字，首字是 VOLTAGE_SELECT */
#define RP1_PADS_PIN0       0x04
#define RP1_PAD_PIN_STRIDE  0x04
#define RP1_PAD_PULL_LSB    2
#define RP1_PAD_PULL_MASK   0x0Cu
#define RP1_PAD_IN_ENABLE   0x40u
#define RP1_PAD_OUT_DISABLE 0x80u

/* 方向组合标志（gpio.h 的 GPIO_FUNC_INPUT/OUTPUT）：bit4=方向有效、
 * bit5=输出 */
#define GPIO_DIR_FLAG 0x10
#define GPIO_DIR_OUT  0x20
#define GPIO_FSEL_RIO 5

static inline u32 rp1_rd(u32 off) {
  return *(volatile u32*)(uintptr_t)(RP1_BASE + off);
}
static inline void rp1_wr(u32 off, u32 v) {
  *(volatile u32*)(uintptr_t)(RP1_BASE + off) = v;
  dsb();
}

static inline u32 gpio_bank(u32 pin) {
  if (pin < 28) return 0;
  if (pin < 34) return 1;
  return 2;
}
static inline u32 gpio_bank_pin(u32 pin) {
  if (pin < 28) return pin;
  if (pin < 34) return pin - 28;
  return pin - 34;
}
/* IO_BANK 该脚的 CTRL 寄存器 */
static inline u32 gpio_ctrl_off(u32 pin) {
  return RP1_IO_BANK_OFF + gpio_bank(pin) * RP1_BANK_STRIDE +
         gpio_bank_pin(pin) * RP1_GPIO_PIN_STRIDE + RP1_GPIO_CTRL;
}
/* SYS_RIO 该脚所属 bank 的位图寄存器（reg + 原子别名） */
static inline u32 gpio_rio_off(u32 pin, u32 reg, u32 alias) {
  return RP1_SYS_RIO_OFF + gpio_bank(pin) * RP1_BANK_STRIDE + reg + alias;
}
/* PADS_BANK 该脚的 pad 寄存器 */
static inline u32 gpio_pad_off(u32 pin) {
  return RP1_PADS_BANK_OFF + gpio_bank(pin) * RP1_BANK_STRIDE +
         RP1_PADS_PIN0 + gpio_bank_pin(pin) * RP1_PAD_PIN_STRIDE;
}

void gpio_config(u32 gpio, u32 pin, u32 val) {
  (void)gpio; /* RP1 单一控制器 */
  if (pin >= RP1_NUM_GPIOS) return;

  u32 fsel = val & RP1_CTRL_FUNCSEL_MASK;

  /* 方向必须先于复用功能设置，否则引脚会按 RIO_OE 的残留值驱动
   * （linux rp1_gpio_direction_input/_output 同序） */
  if (val & GPIO_DIR_FLAG) {
    u32 alias = (val & GPIO_DIR_OUT) ? RP1_SET_OFFSET : RP1_CLR_OFFSET;
    rp1_wr(gpio_rio_off(pin, RP1_RIO_OE, alias),
           1u << gpio_bank_pin(pin));
    fsel = GPIO_FSEL_RIO;
  }

  /* pad 双向放行：清 OUT_DISABLE、置 IN_ENABLE */
  u32 pad = rp1_rd(gpio_pad_off(pin));
  pad &= ~RP1_PAD_OUT_DISABLE;
  pad |= RP1_PAD_IN_ENABLE;
  rp1_wr(gpio_pad_off(pin), pad);

  u32 ctrl = rp1_rd(gpio_ctrl_off(pin));
  ctrl &= ~(RP1_CTRL_FUNCSEL_MASK | RP1_CTRL_OUTOVER_MASK |
            RP1_CTRL_OEOVER_MASK);
  if (fsel == GPIO_FUNC_NONE) {
    /* 无外设驱动：停成输入态 */
    ctrl |= RP1_OEOVER_DISABLE << RP1_CTRL_OEOVER_LSB;
    ctrl |= RP1_CTRL_FUNCSEL_MASK; /* FSEL_NONE_HW == 0x1f */
  } else {
    ctrl |= RP1_OUTOVER_PERI << RP1_CTRL_OUTOVER_LSB;
    ctrl |= RP1_OEOVER_PERI << RP1_CTRL_OEOVER_LSB;
    ctrl |= fsel << RP1_CTRL_FUNCSEL_LSB;
  }
  rp1_wr(gpio_ctrl_off(pin), ctrl);
}

void gpio_pull(u32 gpio, u32 pin, u32 val) {
  (void)gpio;
  if (pin >= RP1_NUM_GPIOS) return;
  u32 pad = rp1_rd(gpio_pad_off(pin));
  pad &= ~RP1_PAD_PULL_MASK;
  pad |= (val << RP1_PAD_PULL_LSB) & RP1_PAD_PULL_MASK;
  rp1_wr(gpio_pad_off(pin), pad);
}

void gpio_output(u32 gpio, u32 pin, u32 val) {
  (void)gpio;
  if (pin >= RP1_NUM_GPIOS) return;
  u32 alias = val ? RP1_SET_OFFSET : RP1_CLR_OFFSET;
  rp1_wr(gpio_rio_off(pin, RP1_RIO_OUT, alias), 1u << gpio_bank_pin(pin));
}

u32 gpio_input(u32 gpio, u32 pin) {
  (void)gpio;
  if (pin >= RP1_NUM_GPIOS) return 0;
  return (rp1_rd(gpio_rio_off(pin, RP1_RIO_IN, RP1_RW_OFFSET)) &
          (1u << gpio_bank_pin(pin))) != 0;
}

void gpio_init_device(device_t* dev) {
  (void)dev;
  /* RP1 窗口已由 platform_map() 预映射（0x1F00000000 前 1MB，含
   * IO_BANK/SYS_RIO/PADS），无需再做映射 */
}
