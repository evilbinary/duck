#ifndef GPIO_H
#define GPIO_H

#include "libs/include/types.h"

// Raspberry Pi 5 (BCM2712 + RP1)
// 【Pi5 的外设不在 SoC 的 legacy 窗口】0xFC000000（BCM2711 那套）在 Pi5 上访问
// 不到：Pi5 的 UART/GPIO 等都在 RP1 南桥里，RP1 经 PCIe 挂在 BCM2712 上，
// ARM 侧窗口从 0x1C00000000 起（固件串口日志第一行 "RP1_UART 0000001c00030000"
// 就是铁证；DTB 里另有 0x1F00000000 的非预取 BAR0 窗口，不是外设空间）。
// 旧值 0xFC000000 的后果：uart_send() 往 0xFC201000 写，Pi5 上到不了串口 ——
// 真机日志停在固件的 "Device tree loaded" 之后、内核一个字节都不出。
#if defined(RASPI5)
#define MMIO_BASE 0x1C00000000ULL
#elif defined(RASPI_Z_W)
#define MMIO_BASE 0x20000000
#elif defined(RASPI2)
#define MMIO_BASE 0x3F000000
#elif defined(RASPI3)
#define MMIO_BASE 0x3F000000
#elif defined(RASPI4)
#define MMIO_BASE 0xFE000000
#else
#define MMIO_BASE 0x3F000000
#endif

#define MMIO_LENGTH 0x01000000

#if defined(RASPI5)
// 【Pi5 窗口布局 —— 真机实测 + DTB（bcm2712-rpi-5-b.dtb）】
//   RP1 南桥（经 PCIe 暴露给 CPU）：窗口基址 0x1C00000000
//     【证据】固件自己的串口日志第一行打印的是：
//         RP1_UART 0000001c00030000      ← 固件就是用这个地址写 RP1 UART0 的
//     DTB 的 pcie ranges 里确实有两个窗口：0x1F00000000（非预取 BAR0，~4GB）
//     与 0x1C00000000（预取，12GB）—— RP1 外设空间走的是 0x1C 这个。
//     UART0      = +0x030000   (rp1/serial@30000, PL011)
//     IO_BANK0   = +0x0D0000   (rp1/gpio@d0000 第 1 段：功能选择/状态)
//     RIO        = +0x0E0000   (第 2 段：输出/使能，含 SET/CLR 别名)
//     PADS_BANK0 = +0x0F0000   (第 3 段：电气特性/上下拉)
//   SoC 侧外设窗口 0x7C000000-0x80000000：
//     UART10 = 0x7D001000      (serial@7d001000，DTB chosen/stdout-path=serial10)
//     GIC-400 = 0x7FFF9000     (compatible = "arm,gic-400")
#define RP1_UART0_BASE      0x1C030000ULL
#define RP1_IO_BANK0_BASE   0x1C0D0000ULL
#define RP1_RIO_BASE        0x1C0E0000ULL
#define RP1_PADS_BANK0_BASE 0x1C0F0000ULL
#define RP1_WINDOW_BASE     0x1C00000000ULL
#define RP1_WINDOW_LENGTH   0x00100000ULL /* 1MB：覆盖上面 4 个块 */
#define SOC_UART10_BASE     0x7D001000ULL
#define SOC_UART10_LENGTH   0x00001000ULL
#define SOC_GIC_LENGTH      0x00007000ULL /* GICD 0x7FFF9000 → 0x80000000 */
#endif

// GPIO registers
// 【注意】以下是 BCM2711 风格的 GPFSEL/GPPUD 寄存器，Pi5 上不存在：
// RP1 的 GPIO 是 IO_BANK0(功能选择, 窗口+0xD0000) + PADS_BANK0(电气, +0xF0000)
// + RIO(输出/使能, +0xE0000) 另一套语义。这里保留只是为了 raspi5/init.c 里
// 那个【已被注释掉】的 uart_init() 能编译；实际引脚复用现在依赖固件
// （config.txt 的 enable_uart=1 + uart0-pi5 overlay 已经把 GPIO14/15 配好）。
// 若以后要让内核自己配引脚，需要按 RP1 的 IO_BANK0/PADS/RIO 重写。
#define GPFSEL0         ((volatile unsigned int*)(MMIO_BASE+0x00200000))
#define GPFSEL1         ((volatile unsigned int*)(MMIO_BASE+0x00200004))
#define GPFSEL2         ((volatile unsigned int*)(MMIO_BASE+0x00200008))
#define GPFSEL3         ((volatile unsigned int*)(MMIO_BASE+0x0020000C))
#define GPFSEL4         ((volatile unsigned int*)(MMIO_BASE+0x00200010))
#define GPFSEL5         ((volatile unsigned int*)(MMIO_BASE+0x00200014))
#define GPSET0          ((volatile unsigned int*)(MMIO_BASE+0x0020001C))
#define GPSET1          ((volatile unsigned int*)(MMIO_BASE+0x00200020))
#define GPCLR0          ((volatile unsigned int*)(MMIO_BASE+0x00200028))
#define GPLEV0          ((volatile unsigned int*)(MMIO_BASE+0x00200034))
#define GPLEV1          ((volatile unsigned int*)(MMIO_BASE+0x00200038))
#define GPEDS0          ((volatile unsigned int*)(MMIO_BASE+0x00200040))
#define GPEDS1          ((volatile unsigned int*)(MMIO_BASE+0x00200044))
#define GPHEN0          ((volatile unsigned int*)(MMIO_BASE+0x00200064))
#define GPHEN1          ((volatile unsigned int*)(MMIO_BASE+0x00200068))

// Pi 5: no GPPUD/GPPUDCLK, pull-up/down is controlled per-pin here.
// Pin n uses bits [2n, 2n+1] in GPIO_PUP_PDN_CNTRL_REG(n/16).
#define GPIO_PUP_PDN_CNTRL_REG0 ((volatile unsigned int*)(MMIO_BASE+0x002000E4))
#define GPIO_PUP_PDN_CNTRL_REG1 ((volatile unsigned int*)(MMIO_BASE+0x002000E8))
#define GPIO_PUP_PDN_CNTRL_REG2 ((volatile unsigned int*)(MMIO_BASE+0x002000EC))
#define GPIO_PUP_PDN_CNTRL_REG3 ((volatile unsigned int*)(MMIO_BASE+0x002000F0))

// PL011 UART registers
// Pi5：UART0 在 RP1 内（窗口 +0x30000），寄存器偏移与 PL011 完全一致。
// 其它平台：SoC 内 UART0（MMIO_BASE + 0x201000）。
#if defined(RASPI5)
#define UART0_BASE      (MMIO_BASE + 0x0030000ULL)
#else
#define UART0_BASE      (MMIO_BASE + 0x00201000)
#endif
#define UART0_DR        ((volatile unsigned int*)(UART0_BASE + 0x000))
#define UART0_FR        ((volatile unsigned int*)(UART0_BASE + 0x018))
#define UART0_IBRD      ((volatile unsigned int*)(UART0_BASE + 0x024))
#define UART0_FBRD      ((volatile unsigned int*)(UART0_BASE + 0x028))
#define UART0_LCRH      ((volatile unsigned int*)(UART0_BASE + 0x02C))
#define UART0_CR        ((volatile unsigned int*)(UART0_BASE + 0x030))
#define UART0_IMSC      ((volatile unsigned int*)(UART0_BASE + 0x038))
#define UART0_ICR       ((volatile unsigned int*)(UART0_BASE + 0x044))

#if defined(RASPI5)
// 固件控制台（SoC UART10，同为 PL011）：DTB 的 chosen/stdout-path = serial10，
// 固件日志从它出来，所以这个地址一定有效。bring-up 阶段两个串口都写。
#define UART10_DR       ((volatile unsigned int*)(SOC_UART10_BASE + 0x000))
#define UART10_FR       ((volatile unsigned int*)(SOC_UART10_BASE + 0x018))
#define UART10_IBRD     ((volatile unsigned int*)(SOC_UART10_BASE + 0x024))
#define UART10_FBRD     ((volatile unsigned int*)(SOC_UART10_BASE + 0x028))
#define UART10_LCRH     ((volatile unsigned int*)(SOC_UART10_BASE + 0x02C))
#define UART10_CR       ((volatile unsigned int*)(SOC_UART10_BASE + 0x030))
#endif

// EMMC2 (SD host) registers —— Pi5 上未经真机验证（RP1 窗口内偏移可能不同）
#define EMMC_BASE       (MMIO_BASE+0x00300000)

// Raspberry Pi 5 用 GIC-400（在 SoC 窗口 0x7C000000-0x80000000 内）。
// 【地址来源】真机 DTB：
//   interrupt-controller@7fff9000 { compatible = "arm,gic-400";
//     reg = <0x7fff9000 0x1000  0x7fffa000 0x2000
//            0x7fffc000 0x2000  0x7fffe000 0x2000>; }
// 原来的 0x1FFF90000 / 0x1FFFA0000 是抄错了一位（0x7FFF9000 vs 0x1FFF90000）。
#define GICD_BASE       0x7FFF9000ULL
#define GICC_BASE       0x7FFFA000ULL

#define GICD_CTLR       ((volatile unsigned int*)(GICD_BASE + 0x000))
#define GICD_ISENABLER  ((volatile unsigned int*)(GICD_BASE + 0x100))
#define GICD_ICENABLER  ((volatile unsigned int*)(GICD_BASE + 0x180))
#define GICD_ICPENDR    ((volatile unsigned int*)(GICD_BASE + 0x280))
#define GICD_IPRIORITYR ((volatile unsigned int*)(GICD_BASE + 0x400))
#define GICD_SGIR       ((volatile unsigned int*)(GICD_BASE + 0xF00))

#define GICC_CTLR       ((volatile unsigned int*)(GICC_BASE + 0x000))
#define GICC_PMR        ((volatile unsigned int*)(GICC_BASE + 0x004))
#define GICC_IAR        ((volatile unsigned int*)(GICC_BASE + 0x00C))
#define GICC_EOIR       ((volatile unsigned int*)(GICC_BASE + 0x010))

// Architectural timer virtual interrupt (CNTVIRQ) is PPI 27 on GIC-400
#define GIC_PPI_CNTVIRQ 27
#define GIC_SPURIOUS_ID 1023

// Pi 5 secondary cores are held by firmware spin-tables at 0xd8 + 8*core
#define SPIN_RELEASE_BASE 0xd8

// Function declarations
void uart_send(u8 c);
unsigned int uart_receive(void);
void timer_init(int hz);
void timer_end(void);
u32 read_core_timer_pending(int cpu);

#endif
