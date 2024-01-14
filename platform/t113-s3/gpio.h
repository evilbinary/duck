#ifndef GPIO_H
#define GPIO_H

#define GPIO_BASE   0x02000000


#define UART0_BASE 0x02500000
#define UART1_BASE 0x02500400
#define UART2_BASE 0x02500800
#define UART3_BASE 0x02500C00


#define UART_USR  0x7c  //UART Status Register
#define UART_LSR   0x14  // UART Line Status Register


#define UART_RECEIVE  0x1
#define UART_TRANSMIT 0x1 << 1


#endif