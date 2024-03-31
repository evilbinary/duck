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


typedef struct f1c200s_vic {
  volatile unsigned int vector;    /* 00 */
  volatile unsigned int base_addr; /* 04 */
  volatile unsigned int rscv;      /* 08 */
  volatile unsigned int nmi_ctrl;  /* 0c */
  volatile unsigned int pend[4];   /* 10 */
  volatile unsigned int en[4];     /* 20 */
  volatile unsigned int mask[4];   /* 30 */
  volatile unsigned int resp[4];   /* 40 */
  volatile unsigned int ff[4];     /* 50 */
  volatile unsigned int prio[4];   /* 50 */
} f1c200s_vic_t;


#define VIC_BASE ((struct f1c200s_vic *)0x01C20400)


#endif