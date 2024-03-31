#ifndef GPIO_H
#define GPIO_H

#define GPIO_BASE 0x01C20800

#define UART0_BASE 0x01C25000
#define UART1_BASE 0x01C25400
#define UART2_BASE 0x01C25800

#define UART_USR 0x7c  // UART Status Register
#define UART_LSR 0x14  // UART Line Status Register

#define UART_RECEIVE 0x1
#define UART_TRANSMIT 0x1 << 1


/** timer*/
struct f1c200s_timer {
  volatile unsigned int irq_ena;    /* 00 */
  volatile unsigned int irq_status; /* 04 */
  int __pad1[2];
  volatile unsigned int t0_ctrl; /* 10 */
  volatile unsigned int t0_ival; /* 14 */
  volatile unsigned int t0_cval; /* 18 */
  int __pad2;
  volatile unsigned int t1_ctrl; /* 20 */
  volatile unsigned int t1_ival; /* 24 */
  volatile unsigned int t1_cval; /* 28 */
};

#define TIMER_BASE ((struct f1c200s_timer *)0x01c20c00)

#define CTL_ENABLE 0x01
#define CTL_RELOAD 0x02 /* reload ival */
#define CTL_SRC_32K 0x00
#define CTL_SRC_24M 0x04

#define CTL_PRE_1 0x00
#define CTL_PRE_2 0x10
#define CTL_PRE_4 0x20
#define CTL_PRE_8 0x30
#define CTL_PRE_16 0x40
#define CTL_PRE_32 0x50
#define CTL_PRE_64 0x60
#define CTL_PRE_128 0x70

#define CTL_SINGLE 0x80
#define CTL_AUTO 0x00

#define CLOCK_24M 24000000
#define CLOCK_24M_MS 24000

#define IE_T0 0x01
#define IE_T1 0x02

#define IRQ_TIMER0 13


#endif