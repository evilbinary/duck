#ifndef T113_GPIO_H
#define T113_GPIO_H

#include "types.h"
#include "t113-ccu.h"
#include "t113-de.h"
#include "t113-tcon.h"

#define GPIO_BASE 0x02000000

#define UART0_BASE 0x02500000
#define UART1_BASE 0x02500400
#define UART2_BASE 0x02500800
#define UART3_BASE 0x02500C00

#define UART_USR 0x7c  // UART Status Register
#define UART_LSR 0x14  // UART Line Status Register

#define UART_RECEIVE 0x1
#define UART_TRANSMIT 0x1 << 1



struct t113_s3_timer {
  volatile unsigned int irq_ena;    /* 00 Timer IRQ Enable Register */
  volatile unsigned int irq_status; /* 04 Timer Status Register*/
  int __pad1[2];
  volatile unsigned int t0_ctrl; /* 10 Timer0 Control Register*/
  volatile unsigned int t0_ival; /* 14 Timer0 Interval Value Register*/
  volatile unsigned int t0_cval; /* 18 Timer0 Current Value Register*/
  int __pad2;
  volatile unsigned int t1_ctrl; /* 20 Timer1 Control Register*/
  volatile unsigned int t1_ival; /* 24 Timer1 Interval Value Register*/
  volatile unsigned int t1_cval; /* 28 Timer1 Current Value Register*/
};


#define TIMER_BASE  0x02050000 

#define CCU_BASE 0x02001000  // 0x0200 1000---0x0200 1FFF

#define DMA_BASE 0x03002000

#define MMC_BASE 0x04020000

#define CODEC_BASE 0x02030000

#define RTC_BASE 0x07090000


#define TWI0_BASE 0x02502000
#define TWI1_BASE 0x02502400
#define TWI2_BASE 0x02502800
#define TWI3_BASE 0x02502C00


#define IRQ_AUDIO_CODEC 57
#define IRQ_TIMER0 91
#define IRQ_DMAC 82
#define IRQ_DMACS 83
#define IRQ_GPIOB_NS 101
#define IRQ_GPIOB_S 102


#endif