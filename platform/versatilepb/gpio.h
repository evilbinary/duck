#ifndef GPIO_H
#define GPIO_H

#include "arch/arch.h"

#define GPIO_BASE   0x101E1000
#define TIMER0_BASE (volatile unsigned int*)0x101E2000
// #define TIMER0_BASE 0x13000000

#define TIMER1_BASE 0x101E2020
#define TIMER2_BASE 0x101E3000
#define TIMER3_BASE 0x101E3020

#define SD_BASE  0x10005000

#define TIMER_LOAD    0x00
#define TIMER_VALUE   0x01
#define TIMER_CONTROL 0x02
#define TIMER_INTCTL  0x03
#define TIMER_BGLOAD  0x06

#define TIMER_CTRL_EN	(1 << 7)
#define TIMER_CTRL_FREERUN	(0 << 6)
#define TIMER_CTRL_PERIODIC	(1 << 6)
#define TIMER_CTRL_INTREN	(1 << 5)
#define TIMER_CTRL_DIV1	(0 << 2)
#define TIMER_CTRL_DIV16	(1 << 2)
#define TIMER_CTRL_DIV256	(2 << 2)
#define TIMER_CTRL_32BIT	(1 << 1)
#define TIMER_CTRL_ONESHOT	(1 << 0)


#define PIC_BASE 0x10140000
#define PIC_STATUS     0x0
#define PIC_INT_ENABLE 0x10
#define PIC_INT_CLEAR 0x14
#define PIC_INT_TIMER0 (1 << 4)

#define SIC_IRQ_VIC_BIT (1<<31)

#define SIC_BASE 0x10003000

#define SIC_STATUS     0x0
#define SIC_INT_ENABLE     0x08
#define SIC_INT_ENCLR 0x0C


#define UART0 0x101f1000


/* serial port register offsets */
#define UART_DATA        0x00 
#define UART_FLAGS       0x18
#define UART_INT_ENABLE  0x0e
#define UART_INT_TARGET  0x0f
#define UART_INT_CLEAR   0x11

/* serial port bitmasks */
#define UART_RECEIVE  0x10
#define UART_TRANSMIT 0x20

typedef struct {
	u32 status;	
	u32 r1;
	u32 r2;
	u32 r3;
	u32 enable;
	u32 r5;
	u32 r6;
	u32 r7;
	u32 r8;
	u32 r9;
} pic_regs_t;

typedef struct {
	u32 status;	
	u32 r1;
	u32 enable;
	u32 r3;
	u32 r4;
	u32 r5;
	u32 r6;
	u32 r7;
	u32 r8;
	u32 r9;
} sic_regs_t;


#endif