#include "arch/arch.h"
#include "f1c200s-ccu.h"
#include "gpio.h"

#define VIC_BASE_ADDR 0


static u32 cntfrq[MAX_CPU] = {
    0,
};

void uart_send_char(char c) {
  while ((io_read32(UART1_BASE + UART_USR) & UART_TRANSMIT) == 0);
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
  while ((io_read32(UART1_BASE + UART_LSR) & UART_RECEIVE) == 0);
  return (io_read32(UART1_BASE));
}

void platform_init() { io_add_write_channel(uart_send); }

void platform_map() {
  // map base
  kprintf("platform map\n");

  page_map(GPIO_BASE, GPIO_BASE, 0);
  page_map(UART0_BASE, UART0_BASE, 0);
  page_map(UART1_BASE, UART1_BASE, 0);

  // timer
  page_map(TIMER_BASE, TIMER_BASE, 0);

  // ccu
  page_map(CCU_BASE, CCU_BASE, 0);

  page_map(VIC_BASE_ADDR, VIC_BASE_ADDR, PAGE_DEV | PAGE_RWX);
  page_map(VIC_BASE, VIC_BASE, 0);
  page_map(SD_BASE, SD_BASE, 0);

  kprintf("platform map end\n");
}

void platform_end() {}

void timer_init(int hz) {
  kprintf("timer init %d\n", hz);
  struct f1c200s_timer *hp = TIMER_BASE;

  int cpu = cpu_get_id();

  kprintf("timer init2\n");

  hp->t0_ival = 6000000 / hz;

  kprintf("timer init3\n");

  hp->t0_ctrl = 0; /* stop the timer */
  hp->irq_ena = IE_T0;

  hp->t0_ctrl = CTL_SRC_24M;
  hp->t0_ctrl |= CTL_RELOAD;
  while (hp->t0_ctrl & CTL_RELOAD);

  hp->t0_ctrl |= CTL_ENABLE;

  kprintf("  Timer I val: %x\n", hp->t0_ival);
  kprintf("  Timer C val: %x\n", hp->t0_cval);
  kprintf("  Timer C val: %x\n", hp->t0_cval);

  VIC_BASE->vector= VIC_BASE_ADDR;
  VIC_BASE->base_addr = VIC_BASE_ADDR;

  VIC_BASE->mask[0] = ~(1 << IRQ_TIMER0);
  VIC_BASE->mask[1] = 0xFFFFFFFF;
  VIC_BASE->en[0] = (1 << IRQ_TIMER0);
  VIC_BASE->en[1] = 0;

}

void timer_end() {
  // kprintf("timer end\n");

  struct f1c200s_timer *hp = TIMER_BASE;
  hp->irq_status = IE_T0;
  // kprintf("timer end2\n");
}

int interrupt_get_source(u32 no) {
  no = EX_TIMER;
  return no;
}

void ipi_enable(int cpu) {}

void lcpu_send_start(u32 cpu, u32 entry) {}