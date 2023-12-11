#include "arch/arch.h"
#include "arch/io.h"
#include "gpio.h"
#include "gic2.h"

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

void platform_init() { io_add_write_channel(uart_send); }

void platform_end() {}

void platform_map() {
  // map base
  page_map(MMIO_BASE, MMIO_BASE, 0);
  page_map(MS_BASE_REG_UART0_PA, MS_BASE_REG_UART0_PA, L2_NCNB);

  // map gic
  page_map(0x16000000, 0x16000000, L2_NCNB);
  page_map(0x16002000, 0x16002000, L2_NCNB);
  page_map(0x16001000, 0x16001000, L2_NCNB);

}

void ipi_enable(int cpu) {}

void timer_init(int hz) {
  int cpu = cpu_get_id();
  kprintf("timer init %d\n",cpu);

  int frq=read_cntfrq();
  kprintf("timer frq %d\n",frq);
  cntfrq[cpu] = frq;

  if(cntfrq[cpu]<0){
    cntfrq[cpu]=6000000;
  }

  cntfrq[cpu] = cntfrq[cpu] / hz;
  kprintf("cntfrq %d\n", cntfrq[cpu]);
  write_cntv_tval(cntfrq[cpu]);

  u32 val = read_cntv_tval();
  kprintf("val %d\n", val);
  enable_cntv(1);

  gic_init(0);
}

void timer_end() {
  kprintf("timer end\n");

  write_cntv_tval(cntfrq[0]);

}

void lcpu_send_start(u32 cpu, u32 entry) {}