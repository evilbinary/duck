#include "init.h"

#include "arch/interrupt.h"
#include "arch/pmemory.h"
#include "gpio.h"
#include "v3s-ccu.h"

static void io_write32(uint port, u32 data) { *(u32 *)port = data; }

static u32 io_read32(uint port) {
  u32 data;
  data = *(u32 *)port;
  return data;
}

void uart_send_char(unsigned int c) {
  unsigned int addr = 0x01c28000;  // UART0
  while ((io_read32(addr + 0x14) & (0x1 << 6)) == 0)
    ;
  io_write32(addr + 0x00, c);
}

void uart_send(unsigned int c) {
  if (c == '\n') {
    uart_send_char(c);
    c = '\r';
  }
  uart_send_char(c);
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

  gic_init(0);
  gic_init2();

  // timer_watch();
  // gic_watch();
  // gic_poll();
}

void timer_end() {
  int irq = IRQ_TIMER0;

  timer_ack();
  // kprintf("timer end %d\n",timer_count);
  // gic_handler2();
  gic_irqack2(irq);
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
  io_write32(V3S_CCU_BASE + CCU_CPU_AXI_CFG, val);

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
  io_write32(V3S_CCU_BASE + CCU_PLL_CPU_CTRL, val);
  sdelay(200);

  /* Switch clock source */
  val = (2 << 0) | (1 << 8) | (2 << 16);
  io_write32(V3S_CCU_BASE + CCU_CPU_AXI_CFG, val);

  while (!(io_read32(V3S_CCU_BASE + CCU_PLL_CPU_CTRL) & (1 << 28)))
    ;
}

static uint32_t pll_periph_get_freq(void) {
  uint32_t reg = io_read32(CCU_BASE + CCU_PLL_PERIPH0_CTRL);

  uint32_t mul = (reg >> 8) & 0x1F;
  uint32_t div = (reg >> 4) & 0x3;
  kprintf("mul %d  div %d\n", mul, div);

  return (24000000 * (mul + 1) / (div + 1));
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

void cpu_clock_init(void) {
  u32 reg;
  cpu_clock_set_pll_cpu(1152000000);

  kprintf("cpu rate %d\n", cpu_get_rate(24000000));
  /* pll video - 396MHZ */
  io_write32(V3S_CCU_BASE + CCU_PLL_VIDEO_CTRL, 0x91004107);

  /* pll periph0 - 600MHZ */
  reg = io_read32(V3S_CCU_BASE + CCU_PLL_PERIPH0_CTRL);
  reg = 0;
  reg |= 1 << 31;  // PLL_ENABLE 24MHz*N*K/2
  reg |= 0 << 25;  // PLL_BYPASS_EN  If the bypass is enabled, the PLL output is
                   // 24MHz.
  reg |= 0 << 24;  // PLL_CLK_OUT_EN
  reg &= ~(1 << 18);  // PLL_24M_OUT_EN 0 disable
  reg &= ~(0x1F << 8);
  reg |= 24 << 8;  // PLL_FACTOR_N 24*25*2/2=600MHZ
  reg |= 0 << 4;   // PLL_FACTOR_K
  reg |= 0 << 1;   // PLL_FACTOR_M

  io_write32(V3S_CCU_BASE + CCU_PLL_PERIPH0_CTRL, reg);
  kprintf("clock periph0 %d\n", pll_periph_get_freq());

  // io_write32(V3S_CCU_BASE + CCU_PLL_PERIPH0_CTRL, 0x90041811);
  while (!(io_read32(V3S_CCU_BASE + CCU_PLL_PERIPH0_CTRL) & (1 << 28)))
    ;

  /* ahb1 = pll periph0 / 3, apb1 = ahb1 / 2 */
  reg = io_read32(V3S_CCU_BASE + CCU_AHB_APB0_CFG);
  reg = 0;
  reg |= 3 << 12;  // AHB1_CLK_SRC_SEL PLL_PERIPH0   600/3=200MHZ
  reg &= ~(3 << 8);
  reg |= 0 << 8;  // APB1_CLK_RATIO 00: /2          AHB1/2 =100MHZ
  reg |= 2 << 6;  // AHB1_PRE_DIV 00: /1  10: /3
  reg &= ~(3 << 4);
  reg |= 0 << 4;  // AHB1_CLK_DIV_RATIO 01: /2

  io_write32(V3S_CCU_BASE + CCU_AHB_APB0_CFG, reg);
  // io_write32(V3S_CCU_BASE + CCU_AHB_APB0_CFG, 0x00003180);

  /* mbus  = pll periph0 / 4 */
  reg = io_read32(V3S_CCU_BASE + CCU_MBUS_CLK);
  reg = 0;
  reg |= 1 << 31;  // MBUS_SCLK_GATING  MBUS_CLOCK = Clock Source/Divider M
  reg |= 1 << 24;  // MBUS_SCLK_SRC  01: PLL_PERIPH0(2X)
  reg |= 3 << 0;   // MBUS_SCLK_RATIO_M

  io_write32(V3S_CCU_BASE + CCU_MBUS_CLK, reg);
  // io_write32(V3S_CCU_BASE + CCU_MBUS_CLK, 0x81000003);

  /* Set APB2 to OSC24M/1 (24MHz). */
  reg = io_read32(V3S_CCU_BASE + CCU_APB1_CFG);
  reg = 0;
  reg |= 1 << 24 | 0 << 16 | 0;
  io_write32(V3S_CCU_BASE + CCU_APB1_CFG, reg);

  // Enable TWI0 clock gating
  u32 gate_reg = io_read32(V3S_CCU_BASE + CCU_BUS_CLK_GATE3);
  io_write32(V3S_CCU_BASE + CCU_BUS_CLK_GATE3, gate_reg | 1 << 0);
}

void platform_init() {
  io_add_write_channel(uart_send);
  cpu_clock_init();
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

  // spi0
  page_map(0x01C68000, 0x01C68000, L2_NCNB);
  // dma
  page_map(0x01C02000, 0x01C02000, L2_NCNB);

  // test_cpu_speed();
}

int interrupt_get_source(u32 no) {
  u32 irq = gic_irqwho2();
  no = EX_TIMER;

  if (irq == IRQ_TIMER0) {
    // kprintf("irq timer %d\n", irq);
  } else if (irq == 1023) {
    no = EX_NONE;
    gic_irqack2(irq);
  } else if (irq == IRQ_DMAC) {
    kprintf("irq dma %d\n", irq);

    no = EX_DMA;
    gic_irqack2(irq);
  } else {
    kprintf("irq else %d\n", irq);
  }

  return no;
}

void ipi_enable(int cpu) {}

void lcpu_send_start(u32 cpu, u32 entry) {}

void ipi_send(int cpu, int vec) {}

void ipi_clear(int cpu) {}