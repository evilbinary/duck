#include "arch/arch.h"
#include "arch/io.h"
#include "gpio.h"

void io_write32(uint port, u32 data) { *(u32 *)port = data; }

u32 io_read32(uint port) {
  u32 data;
  data = *(u32 *)port;
  return data;
}

// UART0 PL011 PrimeCell on Versatile/PB
void uart_send_char(unsigned int c) {
  while (io_read32(UART0 + UART_FLAGS) & UART_TRANSMIT)
    ;
  io_write32(UART0 + UART_DATA, c);
}

void uart_send(unsigned int c) {
  if (c == '\n') {
    uart_send_char(c);
    c = '\r';
  }
  uart_send_char(c);
}

unsigned int uart_receive() {
  unsigned int c;

  while ((io_read32(UART0 + UART_FLAGS) & UART_RECEIVE)) {
  };
  c = io_read32(UART0 + UART_DATA);

  return c;
}

void platform_init() { io_add_write_channel(uart_send); }

void platform_map() {
  // map base
  page_map(GPIO_BASE, GPIO_BASE, PAGE_DEV);
  page_map(UART0, UART0, PAGE_DEV);
}

void platform_end() {}

void timer_init(int hz) {}

void timer_end() {}

void ipi_enable(int cpu) {}

void lcpu_send_start(u32 cpu, u32 entry) {}