#include "arch/arch.h"
#include "arch/io.h"
#include "gic2.h"
#include "gpio.h"

#define IRQ_TIMER0 27

static u32 cntfrq[MAX_CPU] = {
    0,
};

char uart_read() {
  char c;
  while (!(UART_REG8(UART_LSR) & UART_LSR_DR))
    ;
  c = (char)(UART_REG8(UART_TX) & 0xff);
  return c;
}

void uart_send_char(unsigned int c) {
  while (!(UART_REG8(UART_LSR) & UART_LSR_THRE))
    ;
  UART_REG8(UART_TX) = c;
}

void uart_send(unsigned int c) {
  if (c == '\n') {
    uart_send_char(c);
    c = '\r';
  }
  uart_send_char(c);
}


#define REGW16(a,v) 	(*(volatile unsigned short *)(a) = (v))
#define REGR16(a) 		(*(volatile unsigned short *)(a))

void init_cpu_clock(void) {
  REGW16(MMIO_BASE + (0x1032A4 << 1), 0x78D4);  // set target freq to LPF high
  REGW16(MMIO_BASE + (0x1032A6 << 1), 0x0029);
  REGW16(MMIO_BASE + (0x1032B0 << 1), 0x0001);  // switch to LPF control
  REGW16(MMIO_BASE + (0x1032AA << 1), 0x0006);  // mu[2:0]
  REGW16(MMIO_BASE + (0x1032AE << 1), 0x0008);  // lpf_update_cnt[7:0]
  REGW16(MMIO_BASE + (0x1032B2 << 1), 0x1000);  // from low to high
  REGW16(MMIO_BASE + (0x1032A8 << 1), 0x0000);  // toggle LPF enable
  REGW16(MMIO_BASE + (0x1032A8 << 1), 0x0001);

  while (!(REGR16(MMIO_BASE + (0x1032BA << 1))))
    ;  // polling done

  REGW16(MMIO_BASE + (0x1032A8 << 1), 0x0000);
  REGW16(MMIO_BASE + (0x1032A0 << 1), 0x78D4);  // store freq to LPF low
  REGW16(MMIO_BASE + (0x1032A2 << 1), 0x0029);
}

void platform_init() {
  io_add_write_channel(uart_send);
  init_cpu_clock();
}

void platform_end() {}

void platform_map() {
  // map base
  page_map(MMIO_BASE, MMIO_BASE, PAGE_DEV);
  page_map(MS_BASE_REG_UART0_PA, MS_BASE_REG_UART0_PA, PAGE_DEV);

  // map gic
  page_map(0x16000000, 0x16000000, PAGE_DEV);
  page_map(0x16002000, 0x16002000, PAGE_DEV);
  page_map(0x16001000, 0x16001000, PAGE_DEV);

  // mmc
  page_map(0x1f282000, 0x1f282000, PAGE_DEV);
}

void ipi_enable(int cpu) {}

int timer_count = 0;

void timer_init(int hz) {
  int cpu = cpu_get_id();
  kprintf("timer init %d\n", cpu);

  int frq = read_cntfrq();
  kprintf("timer frq %d\n", frq);
  cntfrq[cpu] = frq / hz;

  if (cntfrq[cpu] <= 0) {
    cntfrq[cpu] = 6000000 / hz;
  }

  // cpu_cli();
  kprintf("cntfrq %d\n", cntfrq[cpu]);
  write_cntv_tval(cntfrq[cpu]);

  u32 val = read_cntv_tval();
  kprintf("val %d\n", val);
  enable_cntv(1);

  gic_init(0);
  gic_enable(0, IRQ_TIMER0);
}

void timer_end() {
  int irq = gic_irqwho();
  gic_irqack(irq);
  write_cntv_tval(cntfrq[0]);
}

void lcpu_send_start(u32 cpu, u32 entry) {
  gic_send_sgi(cpu,0);
}