#ifndef GPIO_H
#define GPIO_H

#define MMIO_BASE 0x10000000


#define UART_BASE MMIO_BASE
#define UART_RX 0
#define UART_TX 1

#define UART_TXD (MMIO_BASE + 0x1000)

#define REG_RHR  0  // read mode: Receive holding reg   
#define REG_THR  0  // write mode: Transmit Holding Reg
#define REG_IER  1  // write mode: interrupt enable reg
#define REG_FCR  2  // write mode: FIFO control Reg
#define REG_ISR  2  // read mode: Interrupt Status Reg
#define REG_LCR  3  // write mode:Line Control Reg
#define REG_MCR  4  // write mode:Modem Control Reg
#define REG_LSR  5  // read mode: Line Status Reg
#define REG_MSR  6  // read mode: Modem Status Reg

#define UART_DLL  0  // LSB of divisor Latch when enabled
#define UART_DLM  1  // MSB of divisor Latch when enabled

#endif