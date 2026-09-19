#include "init.h"

#include "arch/interrupt.h"
#include "arch/pmemory.h"
#include "gpio.h"
#include "libs/include/archcommon/irq_chip.h" /* 统一中断框架：irq_chip_register/irq_set_tick */
#include "v3s-ccu.h"

/* platform/v3s/gic.c 提供的控制器实例（get_active/eoi/mask/priority/type/affinity） */
extern struct irq_chip gicv2_v3s_chip;

static void io_write32(uint port, u32 data) { *(u32 *)port = data; }

static u32 io_read32(uint port) {
  u32 data;
  data = *(u32 *)port;
  return data;
}

void uart_send_char(unsigned int c) {
  unsigned int addr = 0x01c28000;  // UART0
  while ((io_read32(addr + 0x14) & (0x1 << 6)) == 0);
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

  /* 【接入统一中断框架】注册控制器并把"哪个号是 tick"告诉框架：
   * 从此所有外设中断都走 duck/kernel/irq.c 的 desc 表派发（控制器取号 → 查表 →
   * 框架统一 EOI → bottom half），本文件的 interrupt_get_source() 不再参与；
   * 未注册的号会"告警一次 + 自动 mask"（USB OTG 103 那类风暴由此根治）。
   * tick（IRQ_TIMER0）仍走老的 EX_TIMER 槽：do_schedule 需要 interrupt_context_t
   * 且可能切换上下文，perf 也是通过覆盖 exception_handlers[EX_TIMER] 挂钩的。
   * 【为什么放在 gic_init2() 之后】它刚把 GICD/GICC 使能、优先级与 IRQ_TIMER0 打开；
   * 而此处 CPU 中断仍是屏蔽的（cpu_cli 状态），不会在 kernel_init 中段被打断。 */
  /* 【顺序有讲究】先声明 tick 再注册控制器：
   * 反过来的话，在"chip 已注册、tick 还没声明"的窗口里若有中断进来，IRQ_TIMER0
   * 会被当成"未注册号"而**自动 mask** —— 时钟就此永久停掉。
   * 先 set_tick：此时 irq_core_enabled() 还是 0，走老路径，行为与改动前一致；
   * 再 register：两者都就位后框架才开始接管。 */
  irq_set_tick(IRQ_TIMER0);
  irq_chip_register(&gicv2_v3s_chip, 0, 160);

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

  while (!(io_read32(V3S_CCU_BASE + CCU_PLL_CPU_CTRL) & (1 << 28)));
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
  reg |= 1 << 25;  // PLL_BYPASS_EN  If the bypass is enabled, the PLL output is
                   // 24MHz.
  reg |= 0 << 24;  // PLL_CLK_OUT_EN
  reg &= ~(1 << 18);  // PLL_24M_OUT_EN 0 disable
  reg &= ~(0x1F << 8);
  reg |= 24 << 8;  // PLL_FACTOR_N 24*25*(2+1)/2=600MHZ
  reg |= 1 << 4;   // PLL_FACTOR_K k=factor+1
  reg |= 1 << 1;   // PLL_FACTOR_M

  // io_write32(V3S_CCU_BASE + CCU_PLL_PERIPH0_CTRL, reg);
  kprintf("clock periph0 %d\n", pll_periph_get_freq());

  // io_write32(V3S_CCU_BASE + CCU_PLL_PERIPH0_CTRL, 0x90041811);
  while (!(io_read32(V3S_CCU_BASE + CCU_PLL_PERIPH0_CTRL) & (1 << 28)));

  /* ahb1 = pll periph0 / 3, apb1 = ahb1 / 2 */
  reg = io_read32(V3S_CCU_BASE + CCU_AHB_APB0_CFG);
  reg |= 3 << 12;  // AHB1_CLK_SRC_SEL PLL_PERIPH0   600/3=200MHZ
  reg &= ~(3 << 8);
  reg |= 0 << 8;  // APB1_CLK_RATIO 00: /2          AHB1/2 =100MHZ
  reg |= 2 << 6;  // AHB1_PRE_DIV 00: /1  10: /3
  reg &= ~(3 << 4);
  reg |= 1 << 4;  // AHB1_CLK_DIV_RATIO 01: /2

  // io_write32(V3S_CCU_BASE + CCU_AHB_APB0_CFG, reg);
  // io_write32(V3S_CCU_BASE + CCU_AHB_APB0_CFG, 0x00003180);

  /* mbus  = pll periph0 / 4 */
  reg = io_read32(V3S_CCU_BASE + CCU_MBUS_CLK);
  reg |= 1 << 31;  // MBUS_SCLK_GATING  MBUS_CLOCK = Clock Source/Divider M
  reg |= 1 << 24;  // MBUS_SCLK_SRC  01: PLL_PERIPH0(2X)
  reg |= 1 << 0;   // MBUS_SCLK_RATIO_M

  // io_write32(V3S_CCU_BASE + CCU_MBUS_CLK, reg);
  // io_write32(V3S_CCU_BASE + CCU_MBUS_CLK, 0x81000003);

  /* Set APB2 to OSC24M/1 (24MHz). */
  reg = io_read32(V3S_CCU_BASE + CCU_APB1_CFG);
  reg |= 1 << 24;
  reg |= 0 << 16;
  reg |= 0;
  // io_write32(V3S_CCU_BASE + CCU_APB1_CFG, reg);

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
  page_map(MMIO_BASE, MMIO_BASE, PAGE_DEV);
  page_map(UART0_DR, UART0_DR, PAGE_DEV);
  page_map(CORE0_TIMER_IRQCNTL, CORE0_TIMER_IRQCNTL, PAGE_DEV);
  page_map(0x01c0f000, 0x01c0f000, PAGE_DEV);  // fix v3s_transfer_command 2 failed 4294967295

  // ccu -pio timer
  page_map(0x01C20000, 0x01C20000, PAGE_DEV);
  // uart
  page_map(0x01C28000, 0x01C28000, PAGE_DEV);
  // timer
  page_map(0x01C20C00, 0x01C20C00, PAGE_DEV);
  // gic
  page_map(0x01C81000, 0x01C81000, PAGE_DEV);
  page_map(0x01C82000, 0x01C82000, PAGE_DEV);

  // spi0
  page_map(0x01C68000, 0x01C68000, PAGE_DEV);
  // dma
  page_map(0x01C02000, 0x01C02000, PAGE_DEV);

  // emac：V3s 片上以太网（Synopsys DesignWare GMAC 核 + 全志 glue，
  // MAC 在 +0x0000、DMA 在 +0x1000，寄存器见 duck/modules/net/v3s.c）
  // 【两页都要映射】只映射 MAC 页时一访问 DMA 页就 data abort
  page_map(0x01C30000, 0x01C30000, PAGE_DEV);
  page_map(0x01C31000, 0x01C31000, PAGE_DEV);
  // system control：EMAC-EPHY 时钟寄存器 @0x01C00030（选择内部 PHY/SMI/MII 并上电）
  page_map(0x01C00000, 0x01C00000, PAGE_DEV);

  // test_cpu_speed();
}

/* 最近一次从 GICC_IAR 取到的原始 INTID（1023 = spurious）。
 * 供 interrupt_ack_pending() 在"中断处理中再异常/提前返回"时补 EOI —— 与
 * t113-s3 平台（duck/platform/t113-s3/init.c 的同名实现）保持一致。
 * 【非 static 的原因】走统一中断框架后，claim 动作发生在
 * platform/v3s/gic.c 的 v3s_gic_get_active() 里，那边要写这个变量；
 * 老路径（interrupt_get_source）下则由本文件自己写。 */
u32 interrupt_last_irq = 1023u;

/* 【健壮性·IRQ 兜底】把当前 active 的中断补一次 EOI。
 * duck/kernel/exceptions.c 里的同名函数是 __attribute__((weak)) 空实现，不覆盖它
 * 的话，"中断处理没走到 EOI 就异常/提前返回"的中断会一直 active。DMA 中断是电平触发
 * 的：挂起位没清干净/EOI 没补上时，GIC 会不停重复投递 ⇒ 实测音频 DMA 出现
 * 11324 次/秒的中断风暴（回调被反复调用、每次只搬进一两个字，DAC 拿不到连续数据）。
 * GIC 规范：EOIR 写入与当前最高优先级 active 中断不匹配时会被忽略 ⇒ 正常路径已经
 * ack 过的情况下重复调用是安全的。 */
void interrupt_ack_pending(void) {
  if (interrupt_last_irq != 1023u) {
    gic_irqack2(interrupt_last_irq);
  }
}

/* 【已由统一中断框架取代】平台注册了 gicv2_v3s_chip 之后（见 timer_init），
 * kernel/exceptions.c 的 EX_IRQ 分支走 irq_claim_and_dispatch()，这个函数不会再被调用。
 * 保留它有两个作用：① 未注册 chip 的平台/早期阶段仍可用；② 它是"IRQ 号 → 逻辑源"
 * 的老参考实现（含历史上踩过的注释）。 */
int interrupt_get_source(u32 no) {
  u32 irq = gic_irqwho2();
  interrupt_last_irq = irq;
  no = EX_TIMER;

  if (irq == IRQ_TIMER0) {
    // kprintf("irq timer %d\n", irq);
  } else if (irq == 1023) {
    no = EX_NONE;
    gic_irqack2(irq);
  } else if (irq == IRQ_DMAC) {
    /* 【只打前 8 次】这是 DMA 中断的热路径：音频播放时约每 23ms 一次，每次到这里
     * 都 kprintf（115200 下 12 字节≈1ms 阻塞、且在中断上下文里）会把 CPU 时间
     * 大量耗在串口上，也把启动日志淹掉。前几次足够证明"中断真的送达"。 */
    {
      static u32 dbg_dma_irq;
      if (dbg_dma_irq < 8u) {
        dbg_dma_irq++;
        kprintf("irq dma %d\n", irq);
      }
    }

    no = EX_DMA;
    gic_irqack2(irq);
  } else {
    /* 【别无限打印】实测 USB OTG_Device(103) 会持续触发且没人处理：115200 下每条
     * kprintf 要 ~1ms 且阻塞，会把串口和 CPU 全吃掉（日志里上万行 "irq else 103"）。
     * 只报前 8 次用来暴露"有中断没被处理"，之后静默 —— 与上面 IRQ_DMAC 的处理一致。 */
    static u32 dbg_irq_else;
    if (dbg_irq_else < 8u) {
      dbg_irq_else++;
      kprintf("irq else %d (unhandled, 之后静默)\n", irq);
    }
  }

  return no;
}

void ipi_enable(int cpu) {}

void lcpu_send_start(u32 cpu, u32 entry) {}

void ipi_send(int cpu, int vec) {}

void ipi_clear(int cpu) {}