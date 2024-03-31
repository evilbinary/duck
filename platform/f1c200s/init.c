#include "arch/arch.h"
#include "gpio.h"

void uart_send_char(char c) {
  while ((io_read32(UART1_BASE + UART_USR) & UART_TRANSMIT) == 0)
    ;
  io_write32(UART1_BASE, c);
}

void uart_send(unsigned int c) {
  if (c == '\n') {
    uart_send_char(c);
    c = '\r';
  }
  uart_send_char(c);
}

u32 uart_receive() {
  while ((io_read32(UART1_BASE + UART_LSR) & UART_RECEIVE) == 0)
    ;
  return (io_read32(UART1_BASE));
}



void platform_init() {
  io_add_write_channel(uart_send);

  timer_init(1000);
}

void platform_map() {
  // map base
  kprintf("platform map\n");

  page_map(GPIO_BASE, GPIO_BASE, PAGE_DEV);
  page_map(UART0_BASE, UART0_BASE, PAGE_DEV);

  kprintf("platform map end\n");
}

void platform_end() {}


void timer_init(int hz) {
  kprintf("timer init %d\n", hz);

  
}

void timer_end() {
  // kprintf("timer end\n");
}

int interrupt_get_source(u32 no) {
  no = EX_TIMER;
  return no;
}

void ipi_enable(int cpu) {}

void lcpu_send_start(u32 cpu, u32 entry) {}