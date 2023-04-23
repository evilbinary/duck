#include "init.h"
#include "arch/pmemory.h"
#include "gpio.h"

static void io_write32(uint port, u32 data) { *(u32 *)port = data; }

static u32 io_read32(uint port) {
  u32 data;
  data = *(u32 *)port;
  return data;
}

void io_write8(uint port, u8 data) { *(u8 *)port = data; }

u8 io_read8(uint port) {
  u8 data;
  data = *(u8 *)port;
  return data;
}

void uart_send_ch(unsigned int c) {
  while ((io_read8(UART_BASE + REG_LSR) & (1 << 5)) == 0)
    ;
  io_write32(UART_BASE + REG_THR, c);
}

void uart_send(unsigned int c) {
  if (c == '\n') {
    uart_send_ch(c);
    c = '\r';
  }
  uart_send_ch(c);
}

unsigned int uart_receive() {
  char c;
  c = io_read8(UART_BASE + REG_LSR);

  if (c & 1) {
    return c;
  }
  return -1;
}

void timer_init(int hz) { kprintf("timer init %d\n", hz); }

void timer_end() {}

void platform_init() {
  io_add_write_channel(uart_send);
  // sys_dram_init();
}

void platform_end() {}

void platform_map() {}

void ipi_enable(int cpu) {}

void lcpu_send_start(u32 cpu, u32 entry) {}

void ipi_send(int cpu, int vec) {}

void ipi_clear(int cpu) {}