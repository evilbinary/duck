#include "arch/arch.h"
#include "arch/io.h"
#include "gpio.h"

char uart_read() {
  char c;
  while (!(UART_REG8(UART_LSR) & UART_LSR_DR))
    ;
  c = (char)(UART_REG8(UART_TX) & 0xff);
  return c;
}

void uart_send_char(unsigned int c) {
  while (!(UART_REG8(UART_LSR) & UART_LSR_THRE))
    ;
  UART_REG8(UART_TX) = c;
}

void uart_send(unsigned int c) {
  if (c == '\n') {
    uart_send_char(c);
    c = '\r';
  }
  uart_send_char(c);
}

void platform_init() { 
  io_add_write_channel(uart_send); 
}

void platform_end() {}

void platform_map() {
  // map base
  page_map(MMIO_BASE, MMIO_BASE, 0);
  page_map(MS_BASE_REG_UART0_PA, MS_BASE_REG_UART0_PA, L2_NCNB);

  // map gic
  page_map(0x16000000, 0x16000000, L2_NCNB);
}

void ipi_enable(int cpu) {}

void timer_init(int hz) {}

void timer_end() {}

void lcpu_send_start(u32 cpu, u32 entry) {}