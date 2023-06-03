#include "init.h"

#include "archcommon/gic2.h"
#include "gpio.h"
#include "v3s-reg-ccu.h"
#include "arch/pmemory.h"

#define TIMER_MASK 0x40000
#define IRQ_UART0 32
#define IRQ_TIMER0 50
#define IRQ_ETHER 114

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

extern int timer_count;

void timer_init(int hz) {
  kprintf("timer init %d\n", hz);
  timer_count = 0;
  ccnt_enable(0);
  ccnt_reset();
  timer_init2(hz);
  gic_init();

  // timer_watch();
  // gic_watch();
  // gic_poll();
}

void gic_handler(void) {
#define GIC_DIST_BASE ((struct gic_dist *)0x01c81000)
#define GIC_CPU_BASE ((struct gic_cpu *)0x01c82000)

  struct gic_dist *gp = GIC_DIST_BASE;
  struct gic_cpu *cp = GIC_CPU_BASE;
  int irq;
  irq = cp->ia;
  if (irq == 1023) {
    uart_send('X');
    return;
  }
  if (gp->ispend[1] & TIMER_MASK) {
    // kprintf("GIC iack = %x\n", irq);

    if (irq == IRQ_TIMER0) {
      timer_handler(0);
    }
    gic_irqack(irq);
    // cp->eoi = irq;
    // gic_unpend(IRQ_TIMER0);
  }

  // ms_delay ( 5 );
}

void timer_end() {
  // kprintf("timer end %d\n",timer_count);
  gic_handler();
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
  k = 3;
  m = 2;
  n = 28;
  /* Switch to 24MHz clock while changing cpu pll */
  val = (2 << 0) | (1 << 8) | (1 << 16);
  io_write32(V3S_CCU_BASE + CCU_CPU_AXI_CFG, val);

  /* cpu pll rate = ((24000000 * n * k) >> p) / m */
  val = (0x1 << 31);
  val |= ((p & 0x3) << 16);
  // val |= ((((clk / (24000000 * k / m)) - 1) & 0x1f) << 8);
  val |= ((n - 1) & 0x1f) << 8;
  val |= (((k - 1) & 0x3) << 4);
  val |= (((m - 1) & 0x3) << 0);
  io_write32(V3S_CCU_BASE + CCU_PLL_CPU_CTRL, val);
  sdelay(200);

  /* Switch clock source */
  val = (2 << 0) | (1 << 8) | (2 << 16);
  io_write32(V3S_CCU_BASE + CCU_CPU_AXI_CFG, val);
}

void cpu_clock_init(void) {
  cpu_clock_set_pll_cpu(1008000000);

  /* pll video - 396MHZ */
  io_write32(V3S_CCU_BASE + CCU_PLL_VIDEO_CTRL, 0x91004107);

  /* pll periph0 - 600MHZ */
  io_write32(V3S_CCU_BASE + CCU_PLL_PERIPH0_CTRL, 0x90041811);
  while (!(io_read32(V3S_CCU_BASE + CCU_PLL_PERIPH0_CTRL) & (1 << 28)))
    ;

  /* ahb1 = pll periph0 / 3, apb1 = ahb1 / 2 */
  io_write32(V3S_CCU_BASE + CCU_AHB_APB0_CFG, 0x00003180);

  /* mbus  = pll periph0 / 4 */
  io_write32(V3S_CCU_BASE + CCU_MBUS_CLK, 0x81000003);

  /* Set APB2 to OSC24M/1 (24MHz). */
  io_write32(V3S_CCU_BASE + CCU_AHB2_CFG, 1 << 24 | 0 << 16 | 0);

  // Enable TWI0 clock gating
  u32 gate_reg = io_read32(V3S_CCU_BASE + CCU_BUS_CLK_GATE3);
  io_write32(V3S_CCU_BASE + CCU_BUS_CLK_GATE3, gate_reg | 1 << 0);
}

u32 cpu_get_rate(u32 prate) {
  u32 r, n, k, m, p;
  u32 rate = 0;
  r = io_read32(V3S_CCU_BASE + CCU_PLL_CPU_CTRL);
  n = ((r >> 8) & 0x1f) + 1;
  k = ((r >> 4) & 0x3) + 1;
  m = ((r >> 0) & 0x3) + 1;
  p = (r >> 16) & 0x3;

  // 90001b21
  // 90001521
  kprintf("V3S_CCU_BASE %x n:%d k:%d m:%d p:%d\n", r, n, k, m, p);
  rate = (((prate * n * k) >> p) / m);
  return rate;
}

void platform_init() {
  io_add_write_channel(uart_send);
  uart_send('P');
  uart_send('\n');
  // cpu_clock_init();
  // sys_dram_init();
}

void platform_end() {
  page_map(UART0_DR, UART0_DR, L2_NCB);
  page_map(CORE0_TIMER_IRQCNTL, CORE0_TIMER_IRQCNTL, L2_NCB);
  // memory
  // u32 address = 0x40000000;
  // kprintf("map memory %x ", address);
  // for (int i = 0; i < 0x2000000 / 0x1000; i++) {
  //   page_map(address, address, L2_TEXT_1 | L2_NCB);
  //   address += 0x1000;
  // }
  // kprintf("- %x\n", address);

  // ccu -pio timer
  page_map(0x01C20000, 0x01C20000, L2_NCB);
  // uart
  page_map(0x01C28000, 0x01C28000, L2_NCB);
  // timer
  page_map(0x01C20C00, 0x01C20C00, L2_NCB);
  // gic
  page_map(0x01C81000, 0x01C81000, L2_NCB);
  page_map(0x01C82000, 0x01C82000, L2_NCB);

  uart_send('E');
  uart_send('\n');
}

void platform_map(){

}

void ipi_enable(int cpu) {}

void lcpu_send_start(u32 cpu, u32 entry) {}

void ipi_send(int cpu, int vec) {}

void ipi_clear(int cpu) {}