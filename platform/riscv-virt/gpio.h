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


#define CLINT_BASE 0x2000000L
#define CLINT_MSIP(hartid) (CLINT_BASE + 4 * (hartid))
#define CLINT_MTIMECMP(hartid) (CLINT_BASE + 0x4000 + 8 * (hartid))
#define CLINT_MTIME (CLINT_BASE + 0xBFF8)

#define MSTATUS_MPP_MASK (3 << 11)
#define MSTATUS_MPP_M (3 << 11)
#define MSTATUS_MPP_S (1 << 11)
#define MSTATUS_MPP_U (0 << 11)
#define MSTATUS_MIE (1 << 3)


#define MIE_MEIE (1 << 11) // external
#define MIE_MTIE (1 << 7)  // timer
#define MIE_MSIE (1 << 3)  // software


#define SSTATUS_SIE (1 << 1)

#define SIE_SEIE (1L << 9) // external
#define SIE_STIE (1L << 5) // timer
#define SIE_SSIE (1L << 1) // software

#endif