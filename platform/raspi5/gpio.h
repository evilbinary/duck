#ifndef GPIO_H
#define GPIO_H

#include "libs/include/types.h"

// Raspberry Pi 5 (BCM2712)
// Legacy peripheral mirror is at 0xFC000000, same register offsets as BCM2711.
#if defined(RASPI5)
#define MMIO_BASE 0xFC000000
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

// GPIO registers
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
#define UART0_DR        ((volatile unsigned int*)(MMIO_BASE+0x00201000))
#define UART0_FR        ((volatile unsigned int*)(MMIO_BASE+0x00201018))
#define UART0_IBRD      ((volatile unsigned int*)(MMIO_BASE+0x00201024))
#define UART0_FBRD      ((volatile unsigned int*)(MMIO_BASE+0x00201028))
#define UART0_LCRH      ((volatile unsigned int*)(MMIO_BASE+0x0020102C))
#define UART0_CR        ((volatile unsigned int*)(MMIO_BASE+0x00201030))
#define UART0_IMSC      ((volatile unsigned int*)(MMIO_BASE+0x00201038))
#define UART0_ICR       ((volatile unsigned int*)(MMIO_BASE+0x00201044))

// EMMC2 (SD host) registers
#define EMMC_BASE       (MMIO_BASE+0x00300000)

// Raspberry Pi 5 uses a GIC-400 instead of the BCM2711 local interrupt
// controller. GICD/GICC are above the 32-bit legacy region.
#define GICD_BASE       0x1FFF90000ULL
#define GICC_BASE       0x1FFFA000ULL

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
