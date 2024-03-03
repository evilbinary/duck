#include "arch/arch.h"
#include "ccu.h"
#include "gpio.h"

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

static inline void sdelay(int loops) {
  __asm__ __volatile__(
      "1:\n"
      "subs %0, %1, #1\n"
      "bne 1b"
      : "=r"(loops)
      : "0"(loops));
}

static void cpu_clock_set_pll_cpu(u32 clk) {
  int p = 0;
  int k = 1;
  int m = 1;
  int n = 32;
  u32 val;
  if (clk > 1152000000) {
    k = 2;
  } else if (clk > 768000000) {
    k = 3;
    m = 2;
  }
  // k = 3;
  // m = 2;
  // n = 28;
  /* Switch to 24MHz clock while changing cpu pll */
  val = (2 << 0) | (1 << 8) | (1 << 16);
  io_write32(CCU_BASE + CCU_CPU_AXI_CFG, val);

  /* cpu pll rate = ((24000000 * n * k) >> p) / m */
  val = (0x1 << 31);
  val |= ((p & 0x3) << 16);
  val |= ((((clk / (24000000 * k / m)) - 1) & 0x1f) << 8);
  val |= (((k - 1) & 0x3) << 4);
  val |= (((m - 1) & 0x3) << 0);

  // val |= ((((clk / (24000000 * k / m)) - 1) & 0x1f) << 8);
  // val |= ((n - 1) & 0x1f) << 8;
  // val |= (((k - 1) & 0x3) << 4);
  // val |= (((m - 1) & 0x3) << 0);
  io_write32(CCU_BASE + CCU_PLL_CPU_CTRL, val);
  sdelay(200);

  /* Switch clock source */
  val = (2 << 0) | (1 << 8) | (2 << 16);
  io_write32(CCU_BASE + CCU_CPU_AXI_CFG, val);
}

void cpu_clock_init(void) {
  // cpu_clock_set_pll_cpu(1008000000);

  /* pll video - 396MHZ */
  // io_write32(CCU_BASE + CCU_PLL_VIDEO_CTRL, 0x91004107);

  // enable apb0
  //  write32(CCU_BASE + CCU_APB0_CLK_REG, (2 << 0) | (1 << 8) | (0x03 << 24));
  //  sdelay(1);

  /* Set APB2 to OSC24M/1 (24MHz). */
  // io_write32(CCU_BASE + CCU_AHB2_CFG, 1 << 24 | 0 << 16 | 0);

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
}

void platform_end() {}

void timer_ack(void) {
  struct t113_s3_timer *hp = TIMER_BASE;
  hp->irq_status = IE_T0;
}

void timer_watch(void) {
  struct t113_s3_timer *hp = TIMER_BASE;
  int val;
  int last;
  int del;

  val = hp->t0_cval;

  for (;;) {
    last = val;
    val = hp->t0_cval;
    /*
    del = last - val;
    printf ("  Timer: 0x%08x %d = %d\n", val, val, del );
    */
    kprintf("  Timer: 0x%x %d = %x\n", val, val, hp->irq_status);

    if (hp->irq_status) timer_ack();
    kprintf(" =Timer: 0x%x %d = %x\n", val, val, hp->irq_status);

    // gig_delay ( 2 );
    sdelay(2000);
  }
}

void timer_init(int hz) {
  struct t113_s3_timer *timer = (struct t113_s3_timer *)TIMER_BASE;

  

  int cpu = cpu_get_id();
  kprintf("timer init %d\n", cpu);


  int frq = read_cntfrq();
  kprintf("timer frq %d\n", frq);
  cntfrq[cpu] = frq / hz;

  if (cntfrq[cpu] <= 0) {
    cntfrq[cpu] = 6000000 / hz;
  }

  kprintf("cntfrq %d\n", cntfrq[cpu]);
  // write_cntv_tval(cntfrq[cpu]);

  u32 val = read_cntv_tval();
  kprintf("val %d\n", val);
  enable_cntv(1);

  
  kprintf("timer-1\n");

  timer->t0_ctrl = 0;  // stop the timer

  kprintf("timer0\n");

  timer->t0_ival = frq / hz;  // Timer0 Interval Value

  kprintf("timer1\n");

  timer->t0_ctrl |=  CTL_SRC_24M;  // 6:4 001: /2

  kprintf("timer2\n");

  timer->t0_ctrl |= CTL_RELOAD;  // 2: Reload the Interval value for timer0
  kprintf("timer3\n");

  while ((timer->t0_ctrl >> 1) & 1)
    ;

  timer->t0_ctrl |= CTL_ENABLE;  // 1: Start

  kprintf("  Timer I val: %x\n", timer->t0_ival);
  kprintf("  Timer C val: %x\n", timer->t0_cval);

  // timer->irq_status = 1;
  timer->irq_ena = IE_T0;  // timer 0 irq enable

  gic_init(0x3020000);  // 0x0302 0000---0x0302 FFFF
  gic_enable(0, IRQ_TIMER0);

  // timer_watch();
  // gic_poll(IRQ_TIMER0);

  
}

void timer_end() {
  kprintf("timer end\n");
  int irq = gic_irqwho();
  gic_irqack(irq);
  write_cntv_tval(cntfrq[0]);
}

void ipi_enable(int cpu) {}

void lcpu_send_start(u32 cpu, u32 entry) {}