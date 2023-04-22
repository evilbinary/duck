#include "init.h"

#include "arch/pmemory.h"

static void io_write32(uint port, u32 data) { *(u32 *)port = data; }

static u32 io_read32(uint port) {
  u32 data;
  data = *(u32 *)port;
  return data;
}

void uart_send_ch(unsigned int c) {
  unsigned int addr = 0x01c28000;  // UART0
  while ((io_read32(addr + 0x14) & (0x1 << 6)) == 0)
    ;
  io_write32(addr + 0x00, c);
}

void uart_send(unsigned int c) {
  if (c == '\n') {
    uart_send_ch(c);
    c = '\r';
  }
  uart_send_ch(c);
}

unsigned int uart_receive() {
  unsigned int c = 0;
  unsigned int addr = 0x01c28000;  // UART0
  while ((io_read32(addr + 0x14) & (0x1 << 0)) == 0) {
  }
  c = io_read32(addr + 0x00);
  return c;
}

void timer_init(int hz) {
  kprintf("timer init %d\n", hz);

}

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