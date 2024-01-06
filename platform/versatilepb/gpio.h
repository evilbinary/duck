#ifndef GPIO_H
#define GPIO_H

#define GPIO_BASE   0x101E1000
#define TIMER0_BASE 0x101E2000


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

#endif