#ifndef GPIO_H
#define GPIO_H

#define GET_BASE_ADDR_BY_BANK(x, y)  ((x) + ((y) << 1))

#define MS_BASE_REG_RIU_PA					(0x1F000000)

#define MS_BASE_REG_UART0_PA              	GET_BASE_ADDR_BY_BANK(MS_BASE_REG_RIU_PA, 0x110800) //0x1F221000
#define MS_BASE_REG_TIMER0_PA              	GET_BASE_ADDR_BY_BANK(MS_BASE_REG_RIU_PA, 0x003020)
#define MS_BASE_REG_TIMER2_PA              	GET_BASE_ADDR_BY_BANK(MS_BASE_REG_RIU_PA, 0x003000)

#define MMIO_BASE  MS_BASE_REG_RIU_PA

#define UART_BASE MS_BASE_REG_UART0_PA
#define UART_REG8(_x_)  ((u8 volatile *)(UART_BASE))[((_x_) * 4) - ((_x_) & 1)]

#define UART_RX        (0 * 2)  // In:  Receive buffer (DLAB=0)
#define UART_TX        (0 * 2)  // Out: Transmit buffer (DLAB=0)
#define UART_DLL       (0 * 2)  // Out: Divisor Latch Low (DLAB=1)
#define UART_DLM       (1 * 2)  // Out: Divisor Latch High (DLAB=1)
#define UART_IER       (1 * 2)  // Out: Interrupt Enable Register
#define UART_IIR       (2 * 2)  // In:  Interrupt ID Register
#define UART_FCR       (2 * 2)  // Out: FIFO Control Register
#define UART_LCR       (3 * 2)  // Out: Line Control Register
#define UART_MCR       (4 * 2)  // Out: Modem Control Register
#define UART_LSR       (5 * 2)  // In:  Line Status Register
#define UART_MSR       (6 * 2)  // In:  Modem Status Register
#define UART_USR       (7 * 2)  // Out: USER Status Register

#define UART_FIFO                   1           // Divisor Latch Low
#define UART_EFR                    2              // I/O: Extended Features Register
#define UART_RB                     3           // optional: set rf_pop delay for memack ; [3:0] rf_pop_delay; [6] rf_pop_mode_sel ; [7] reg_rb_read_ack
                                                // (DLAB=1, 16C660 only)
#define UART_SCR                    7           // I/O: Scratch Register
#define UART_SCCR                   8           // Smartcard Control Register
#define UART_SCSR                   9           // Smartcard Status Register
#define UART_SCFC                   10          // Smartcard Fifo Count Register
#define UART_SCFI                   11          // Smartcard Fifo Index Register
#define UART_SCFR                   12          // Smartcard Fifo Read Delay Register
#define UART_SCMR                   13          // Smartcard Mode Register
#define UART_DL                     0           // Divisor Latch
#define UART_DL1_LSB                0           // Divisor Latch Low
#define UART_DL2_MSB                0           // Divisor Latch High


//
// UART_LSR(5)
// Line Status Register
//
#define UART_LSR_DR                 0x01          // Receiver data ready
#define UART_LSR_OE                 0x02          // Overrun error indicator
#define UART_LSR_PE                 0x04          // Parity error indicator
#define UART_LSR_FE                 0x08          // Frame error indicator
#define UART_LSR_BI                 0x10          // Break interrupt indicator
#define UART_LSR_THRE               0x20          // Transmit-hold-register empty
#define UART_LSR_TEMT               0x40          // Transmitter empty

#endif