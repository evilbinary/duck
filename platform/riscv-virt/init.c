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

int timer_val = 0;

void timer_init(int hz) {
  kprintf("timer init %d\n", hz);
  int id = cpu_get_id();

  timer_val = hz;

  // *(u32 *)CLINT_MTIMECMP(id) = *(u32 *)CLINT_MTIME + timer_val;  //
  // *(u32 *)CLINT_MSIP(id) = 1;
  // cpu_write_mstatus(cpu_read_mstatus() | MSTATUS_MIE);
  // cpu_write_mie(cpu_read_mie() | MIE_MTIE);

  cpu_write_stimecmp(*(u32 *)CLINT_MTIME + timer_val);
  cpu_write_sstatus(cpu_read_sstatus() | SSTATUS_SIE);
  cpu_write_sie(cpu_read_sie() | SIE_STIE);

}

void timer_end() {
  kprintf("timer end\n");

  cpu_write_stimecmp(*(u32 *)CLINT_MTIME + timer_val);

  // int id = cpu_get_id();
  // *(u32 *)CLINT_MTIMECMP(id) = *(u32 *)CLINT_MTIME + timer_val;
}

void platform_init() { io_add_write_channel(uart_send); }

void platform_end() {}

void platform_map() {}

void ipi_enable(int cpu) {}

void lcpu_send_start(u32 cpu, u32 entry) {}

void ipi_send(int cpu, int vec) {}

void ipi_clear(int cpu) {}