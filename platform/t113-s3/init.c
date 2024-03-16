#include "arch/arch.h"
#include "t113-ccu.h"
#include "t113-gpio.h"
#include "t113-tcon.h"
#include "t113-de.h"

static u32 cntfrq[MAX_CPU] = {
    0,
};

#define IRQ_TIMER0 91

#define CLOCK_24M 24000000
#define CTL_ENABLE 0x01
#define CTL_RELOAD 0x02 /* reload ival */
#define CTL_SRC_32K 0x00
#define CTL_SRC_24M 0x04

#define CTL_PRE_1 0x00
#define CTL_PRE_2 0x10
#define CTL_PRE_4 0x20
#define CTL_PRE_8 0x30
#define CTL_PRE_16 0x40
#define CTL_PRE_32 0x50
#define CTL_PRE_64 0x60
#define CTL_PRE_128 0x70

#define CTL_SINGLE 0x80
#define CTL_AUTO 0x00

#define IE_T0 0x01
#define IE_T1 0x02

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


void cpu_clock_init(void) {

  sunxi_clk_init();
}

void platform_init() {
  io_add_write_channel(uart_send);
  cpu_clock_init();
}

void platform_map() {
  // uart
  page_map(UART0_BASE, UART0_BASE, 0);

  // map gic
  page_map(0x03020000, 0x03020000, 0);
  page_map(0x03021000, 0x03021000, 0);
  page_map(0x03022000, 0x03022000, 0);
  // timer
  page_map(TIMER_BASE, TIMER_BASE, 0);

  // ccu
  page_map(CCU_BASE, CCU_BASE, 0);

  //mmc
  page_map(0x04020000, 0x04020000, 0);

  //gpio
  page_map(GPIO_BASE,GPIO_BASE,0);


}

void platform_end() {}

void timer_ack(void) {
  struct t113_s3_timer *hp = TIMER_BASE;
  hp->irq_status = IE_T0;
}


void timer_init(int hz) {
  struct t113_s3_timer *timer = (struct t113_s3_timer *)TIMER_BASE;

  int cpu = cpu_get_id();
  kprintf("timer init %d\n", cpu);

  int frq = read_cntfrq();
  kprintf("timer frq %d\n", frq);
  cntfrq[cpu] = frq/hz ;

  if (cntfrq[cpu] <= 0) {
    cntfrq[cpu] = 6000000 / hz;
  }

  kprintf("cntfrq %d\n", cntfrq[cpu]);
  write_cntv_tval(cntfrq[cpu]);

  u32 val = read_cntv_tval();
  kprintf("val %d\n", val);
  enable_cntv(1);

  timer->t0_ctrl = 0;  // stop the timer

  timer->t0_ival = cntfrq[cpu];  // Timer0 Interval Value

  timer->t0_ctrl |= CTL_SRC_24M;  // 6:4 001: /2

  timer->t0_ctrl |= CTL_RELOAD;  // 2: Reload the Interval value for timer0

  while ((timer->t0_ctrl >> 1) & 1)
    ;

  kprintf("  Timer I val: %x\n", timer->t0_ival);
  kprintf("  Timer C val: %x\n", timer->t0_cval);

  timer->t0_ctrl |= CTL_ENABLE;  // 1: Start

  timer->irq_status = 1;
  timer->irq_ena |= IE_T0;  // timer 0 irq enable

  gic_init(0x03020000);  // 0x0302 0000---0x0302 FFFF
  gic_enable(0, IRQ_TIMER0);
}

void timer_end() {
  int irq = gic_irqwho();
  gic_irqack(irq);
  write_cntv_tval(cntfrq[0]);

  struct t113_s3_timer *timer = (struct t113_s3_timer *)TIMER_BASE;

  // kprintf("timer end %d\n",irq);
  timer->irq_status = IE_T0;

}

void ipi_enable(int cpu) {}

void lcpu_send_start(u32 cpu, u32 entry) {}