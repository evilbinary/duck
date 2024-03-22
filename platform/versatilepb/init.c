#include "arch/arch.h"
#include "gpio.h"

#define VIC_BASE 0
// #define VIC_BASE 0xffff0000

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

void platform_init() {
  io_add_write_channel(uart_send);

  timer_init(1000);
}

void platform_map() {
  // map base
  kprintf("platform map\n");

  page_map(GPIO_BASE, GPIO_BASE, PAGE_DEV);
  page_map(UART0, UART0, PAGE_DEV);
  page_map(VIC_BASE, VIC_BASE, PAGE_DEV | PAGE_RWX);
  page_map(TIMER2_BASE, TIMER2_BASE, PAGE_DEV);
  page_map(TIMER0_BASE, TIMER0_BASE, PAGE_DEV);
  page_map(TIMER3_BASE, TIMER3_BASE, PAGE_DEV);

  page_map(PIC_BASE, PIC_BASE, PAGE_DEV);
  page_map(SD_BASE, SD_BASE, PAGE_DEV);

  kprintf("platform map end\n");
}

void platform_end() {}

// 0x101E2000 for Timer 0 0x101E2020 for Timer 1 0x101E3000 for Timer 2
// 0x101E3020 for Timer 3.
void timer_init(int hz) {
  kprintf("timer init %d\n", hz);

  io_write32(TIMER0_BASE + TIMER_LOAD, hz);

  u32 reg = TIMER_CTRL_PERIODIC | TIMER_CTRL_EN | TIMER_CTRL_32BIT |
            TIMER_CTRL_INTREN | TIMER_CTRL_32BIT ;
  io_write32(TIMER0_BASE + TIMER_CONTROL, reg);

  u32 val = io_read32(TIMER0_BASE + TIMER_CONTROL);
  kprintf("value=>%x %x\n", val, reg);

  u32 pic = io_read32(PIC_BASE + PIC_INT_ENABLE);
  pic |= PIC_INT_TIMER0;
  // pic|= (1 << 4) | (1 << 5);
  io_write32(PIC_BASE + PIC_INT_ENABLE, pic);


}

void timer_end() {
  // kprintf("timer end\n");
  io_write32(TIMER0_BASE + TIMER_INTCTL, 0);
}

void ipi_enable(int cpu) {}

void lcpu_send_start(u32 cpu, u32 entry) {}