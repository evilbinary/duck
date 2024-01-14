#include "arch/arch.h"
#include "gpio.h"

void uart_send_char(char c) {

  while ((io_read32(UART0_BASE + UART_USR) & UART_TRANSMIT) == 0)
    ;
  io_write32(UART0_BASE, c);
}

void uart_send(unsigned int c) {
  if (c == '\n') {
    uart_send_char(c);
    c = '\r';
  }
  uart_send_char(c);
}

u32 uart_receive() {
  while ((io_read32(UART0_BASE + UART_LSR) & UART_RECEIVE) == 0)
    ;
  return (io_read32(UART0_BASE));
}

void platform_init() { io_add_write_channel(uart_send); }

void platform_map() {}

void platform_end() {}

void timer_init(int hz) {}

void timer_end() {}

void ipi_enable(int cpu) {}

void lcpu_send_start(u32 cpu, u32 entry) {}