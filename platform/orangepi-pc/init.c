#include "init.h"

#include "arch/pmemory.h"
#include "archcommon/gic2.h"
#include "gpio.h"
#include "h3-ccu.h"

#define TIMER_MASK 0x40000
#define IRQ_UART0 32
#define IRQ_TIMER0 50
#define IRQ_ETHER 114

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

extern int timer_count;

void timer_init(int hz) {
  kprintf("timer init %d\n", hz);
  timer_count = 0;
  ccnt_enable(0);
  ccnt_reset();
  timer0_init(hz);

  gic_init(0);
  // timer_watch();
  // gic_watch();
  // gic_poll();
}

void timer_end() {
  kprintf("timer end %d\n", timer_count);
  gic_handler();
}

void platform_init() {
  io_add_write_channel(uart_send);
  // cpu_clock_init();
  // sys_dram_init();
}

void platform_end() {}

void platform_map() {
  page_map(MMIO_BASE, MMIO_BASE, 0);
  page_map(UART0_DR, UART0_DR, L2_NCNB);
  page_map(CORE0_TIMER_IRQCNTL, CORE0_TIMER_IRQCNTL, L2_NCNB);
  page_map(0x01c0f000, 0x01c0f000,
           0);  // fix v3s_transfer_command 2 failed 4294967295

  // ccu -pio timer
  page_map(0x01C20000, 0x01C20000, L2_NCNB);
  // uart
  page_map(0x01C28000, 0x01C28000, L2_NCNB);
  // timer
  page_map(0x01C20C00, 0x01C20C00, L2_NCNB);
  // gic
  page_map(0x01C81000, 0x01C81000, L2_NCNB);
  page_map(0x01C82000, 0x01C82000, L2_NCNB);
}

void ipi_enable(int cpu) {}

void lcpu_send_start(u32 cpu, u32 entry) {}

void ipi_send(int cpu, int vec) {}

void ipi_clear(int cpu) {}