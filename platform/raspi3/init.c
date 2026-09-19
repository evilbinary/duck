#include "arch/arch.h"
#include "gpio.h"
#include "kernel/page.h"
#include "libs/include/types.h"

/* 统一中断框架：struct irq_chip / irq_chip_register / irq_set_tick */
#include "kernel/irq.h"
#include "libs/include/archcommon/irq_chip.h"

static void io_write32(volatile unsigned int* port, u32 data);
static u32 io_read32(volatile unsigned int* port);
void ipi_clear(int cpu);

static void io_write32(volatile unsigned int* port, u32 data) {
  *port = data;
}

static u32 io_read32(volatile unsigned int* port) {
  return *port;
}

// BCM2835/6 peripheral IRQ controller (GPU interrupts)
#define BCM2835_IRQ_BASE_ADDR   (MMIO_BASE + 0x00B200)
#define IRQ_BASIC_PENDING       ((volatile unsigned int*)(BCM2835_IRQ_BASE_ADDR + 0x00))
#define IRQ_PENDING1            ((volatile unsigned int*)(BCM2835_IRQ_BASE_ADDR + 0x04))
#define IRQ_PENDING2            ((volatile unsigned int*)(BCM2835_IRQ_BASE_ADDR + 0x08))
#define IRQ_DISABLE1            ((volatile unsigned int*)(BCM2835_IRQ_BASE_ADDR + 0x1C))
#define IRQ_DISABLE2            ((volatile unsigned int*)(BCM2835_IRQ_BASE_ADDR + 0x20))
#define IRQ_BASIC_DISABLE       ((volatile unsigned int*)(BCM2835_IRQ_BASE_ADDR + 0x24))

// EMMC interrupt is peripheral IRQ 62 => pending2 bit (62-32)=30.
#define IRQ2_EMMC_BIT           (1u << 30)

/* 使能寄存器（mask/unmask 用；偏移与 DISABLE* 对称）*/
#define IRQ_ENABLE1             ((volatile unsigned int*)(BCM2835_IRQ_BASE_ADDR + 0x10))
#define IRQ_ENABLE2             ((volatile unsigned int*)(BCM2835_IRQ_BASE_ADDR + 0x14))
#define IRQ_BASIC_ENABLE        ((volatile unsigned int*)(BCM2835_IRQ_BASE_ADDR + 0x18))

/* ================================================================== */
/* 统一中断框架的"平台唯一 IRQ 标识"号段（raspi3/BCM2837）              */
/* ================================================================== */
/* BCM2837 是两层结构：每核的本地源（CORE0_IRQ_SOURCE）+ 外设位图（PENDING1/2/BASIC）。
 * 两层都归同一个 chip 管（位图型：框架通过 for_each_pending 取号）：
 *   0           每核虚拟定时器 CNTV —— tick 走这个号（irq_set_tick）
 *   1           每核"其它"源（GPU/PMU/AXI…）：无从解码 ⇒ 报上来让框架计数+告警一次，
 *               并把外设位图整体 mask（沿用老 interrupt_get_source 的兜底，防硬锁死）
 *   32..63      外设 IRQ_PENDING1 位 0..31
 *   64..95      外设 IRQ_PENDING2 位 0..31（EMMC = 位 30 ⇒ 号 94）
 *   96..103     IRQ_BASIC_PENDING 位 0..7（位 8/9 只是 PENDING1/2 的"有东西"镜像，不单独报） */
#define IRQ_RASPI3_CNTV        0
#define IRQ_RASPI3_UNKNOWN     1
#define IRQ_RASPI3_IRQ1_BASE   32
#define IRQ_RASPI3_IRQ2_BASE   64
#define IRQ_RASPI3_BASIC_BASE  96
#define IRQ_RASPI3_IRQ_NUM     128
#define IRQ_RASPI3_IRQ1(n)     (IRQ_RASPI3_IRQ1_BASE + (n))
#define IRQ_RASPI3_IRQ2(n)     (IRQ_RASPI3_IRQ2_BASE + (n))
#define IRQ_RASPI3_BASIC(n)    (IRQ_RASPI3_BASIC_BASE + (n))
/* SD 卡（EMMC）用的号：老路径是"清状态 + 永久关中断"（SD 实际走轮询），
 * 现在交给框架 ⇒ 首次触发会被"告警一次 + 自动 mask"，效果等价且可观测 */
#define IRQ_RASPI3_EMMC        IRQ_RASPI3_IRQ2(30)

// BCM2835 EMMC register block base
#define BCM2835_EMMC_BASE_ADDR  (MMIO_BASE + 0x00300000)
#define EMMC_INTERRUPT_REG      ((volatile unsigned int*)(BCM2835_EMMC_BASE_ADDR + 0x30))
#define EMMC_IRPT_EN_REG        ((volatile unsigned int*)(BCM2835_EMMC_BASE_ADDR + 0x38))

static u64 cntfrq[MAX_CPU] = {0};
static volatile u32 ap_release[MAX_CPU] = {0};

static void delay_cycles(int n) {
  for (volatile int i = 0; i < n; i++) {
  }
}

static void uart_init(void) {
  // GPIO14/15 -> ALT0 (TXD0/RXD0), disable pulls.
  u32 r = io_read32(GPFSEL1);
  r &= ~((7u << 12) | (7u << 15));   // clear fsel for gpio14/gpio15
  r |=  (4u << 12) | (4u << 15);     // alt0
  io_write32(GPFSEL1, r);

  io_write32(GPPUD, 0);
  delay_cycles(1500);
  io_write32(GPPUDCLK0, (1u << 14) | (1u << 15));
  delay_cycles(1500);
  io_write32(GPPUDCLK0, 0);

  // Make sure RX is enabled (do not disturb baud unless it looks uninitialized).
  io_write32(UART0_ICR, 0x7FF);  // clear pending UART interrupts

  u32 ibrd = io_read32(UART0_IBRD);
  u32 fbrd = io_read32(UART0_FBRD);
  if (ibrd == 0 && fbrd == 0) {
    // Fallback for typical 48MHz UARTCLK -> 115200 baud: IBRD=26, FBRD=3.
    io_write32(UART0_CR, 0);
    io_write32(UART0_IBRD, 26);
    io_write32(UART0_FBRD, 3);
    io_write32(UART0_LCRH, (3u << 5) | (1u << 4));  // 8N1, FIFO enable
  } else {
    /* Ensure 8-bit mode + FIFO enable; keep existing divisors.
     * 【必须带 FEN(bit4)】QEMU 的 PL011 模型在 FIFO 未使能时
     * can_receive() 恒为假，主机侧发来的输入根本不会进入 RX FIFO：
     * 实测 LCRH=0x60（无 FEN）时 FR 恒为 0x90（RXFE=1），shell 轮询
     * read(0) 永远返回 0 ⇒ 表现为"shell 完全无法输入"。
     * （boot 阶段写的是 0b11<<5=0x60，同样没有 FEN，所以这里必须补。） */
    u32 lcrh = io_read32(UART0_LCRH);
    lcrh &= ~(3u << 5);
    lcrh |= (3u << 5) | (1u << 4);
    io_write32(UART0_LCRH, lcrh);
  }

  io_write32(UART0_IMSC, 0);  // mask all UART interrupts (polling)

  // Enable UART, TX and RX.
  u32 cr = io_read32(UART0_CR);
  cr |= (1u << 0) | (1u << 8) | (1u << 9);
  io_write32(UART0_CR, cr);

  // Drain any stale RX data.
  while ((io_read32(UART0_FR) & (1u << 4)) == 0) {
    (void)io_read32(UART0_DR);
  }
}

void uart_send(u8 c) {
  while (io_read32(UART0_FR) & 0x20) {
  } 
  io_write32(UART0_DR, c);
}

unsigned int uart_receive(void) {
  unsigned int c;
  while (io_read32(UART0_FR) & 0x10) {
  }
  c = io_read32(UART0_DR) & 0xFF;
  return c;
}

u32 read_core_timer_pending(int cpu) {
  u32 tmp;
  tmp = io_read32((volatile unsigned int*)(CORE0_IRQ_SOURCE + 4 * cpu));
  return tmp;
}

/* ================================================================== */
/* 统一中断框架的控制器实例（位图型：实现 for_each_pending）             */
/* ================================================================== */
/* 设计文档 §8：raspi2/raspi3 的 BCM2835/37 legacy intc 是位图型 ——
 * 框架不"claim"，而是由控制器把所有挂起的号喂进来（for_each_pending 回调）。 */

static void raspi3_irq_mask(u32 irq) {
  if (irq == IRQ_RASPI3_CNTV) {
    /* 每核定时器：清本核的本地源路由（0 = 该核 CNTV/mailbox 全关） */
    io_write32((volatile unsigned int*)(CORE0_TIMER_IRQCNTL + 4 * cpu_get_id()),
               0);
    return;
  }
  if (irq == IRQ_RASPI3_UNKNOWN) {
    /* 沿用老 interrupt_get_source() 的兜底：把当前挂起的外设位图整体关掉。
     * 解不开的源如果不清，IRQ 线会一直拉着 ⇒ 硬锁死（老代码专门为这个写过一段）。 */
    u32 p1 = io_read32(IRQ_PENDING1);
    u32 p2 = io_read32(IRQ_PENDING2);
    u32 b = io_read32(IRQ_BASIC_PENDING);
    if (p1) io_write32(IRQ_DISABLE1, p1);
    if (p2) io_write32(IRQ_DISABLE2, p2);
    if (b) io_write32(IRQ_BASIC_DISABLE, b);
    return;
  }
  if (irq >= IRQ_RASPI3_IRQ1_BASE && irq < IRQ_RASPI3_IRQ1_BASE + 32) {
    io_write32(IRQ_DISABLE1, 1u << (irq - IRQ_RASPI3_IRQ1_BASE));
    return;
  }
  if (irq >= IRQ_RASPI3_IRQ2_BASE && irq < IRQ_RASPI3_IRQ2_BASE + 32) {
    io_write32(IRQ_DISABLE2, 1u << (irq - IRQ_RASPI3_IRQ2_BASE));
    return;
  }
  if (irq >= IRQ_RASPI3_BASIC_BASE && irq < IRQ_RASPI3_BASIC_BASE + 8) {
    io_write32(IRQ_BASIC_DISABLE, 1u << (irq - IRQ_RASPI3_BASIC_BASE));
  }
}

static void raspi3_irq_unmask(u32 irq) {
  if (irq == IRQ_RASPI3_CNTV) {
    io_write32((volatile unsigned int*)(CORE0_TIMER_IRQCNTL + 4 * cpu_get_id()),
               0x08);
    return;
  }
  if (irq >= IRQ_RASPI3_IRQ1_BASE && irq < IRQ_RASPI3_IRQ1_BASE + 32) {
    io_write32(IRQ_ENABLE1, 1u << (irq - IRQ_RASPI3_IRQ1_BASE));
    return;
  }
  if (irq >= IRQ_RASPI3_IRQ2_BASE && irq < IRQ_RASPI3_IRQ2_BASE + 32) {
    io_write32(IRQ_ENABLE2, 1u << (irq - IRQ_RASPI3_IRQ2_BASE));
    return;
  }
  if (irq >= IRQ_RASPI3_BASIC_BASE && irq < IRQ_RASPI3_BASIC_BASE + 8) {
    io_write32(IRQ_BASIC_ENABLE, 1u << (irq - IRQ_RASPI3_BASIC_BASE));
  }
}

/* 控制器把当前所有挂起的号喂给框架：框架对每个号做
 * "查表 → 派发 → （未注册则）计数 + 告警一次 + 自动 mask"。 */
static int raspi3_irq_pending(int (*cb)(u32, void*), void* arg) {
  u32 p1, p2, b, core;
  int n;

  /* ① 外设位图：PENDING2 先报（EMMC 等"会立刻出事"的号在 2 号位图） */
  p2 = io_read32(IRQ_PENDING2);
  for (n = 0; n < 32; n++) {
    if (p2 & (1u << n)) {
      cb(IRQ_RASPI3_IRQ2(n), arg);
    }
  }
  p1 = io_read32(IRQ_PENDING1);
  for (n = 0; n < 32; n++) {
    if (p1 & (1u << n)) {
      cb(IRQ_RASPI3_IRQ1(n), arg);
    }
  }
  b = io_read32(IRQ_BASIC_PENDING) & 0xFFu; /* 只有低 8 位是真源 */
  for (n = 0; n < 8; n++) {
    if (b & (1u << n)) {
      cb(IRQ_RASPI3_BASIC(n), arg);
    }
  }

  /* ② 每核本地源：
   *    mailbox(IPI)：只清不报 —— 它不是设备中断，报上去只会被判"未处理"并触发
   *                  告警 + 自动 mask（对 mailbox 毫无意义）；
   *    其它位      ：报成 UNKNOWN，让框架计数 + 告警一次 + 兜底 mask；
   *    CNTV(tick)  ：**最后**报 —— tick 的 handler（do_schedule）可能切换上下文，
   *                  放最后可保证"切走之后"不再继续派发别的号。 */
  core = read_core_timer_pending(cpu_get_id());
  if (core & 0xF0u) {
    ipi_clear(cpu_get_id());
  }
  if (core & ~(0x08u | 0xF0u)) {
    cb(IRQ_RASPI3_UNKNOWN, arg);
  }
  if (core & 0x08u) {
    cb(IRQ_RASPI3_CNTV, arg);
  }
  return 0;
}

struct irq_chip raspi3_bcm_chip = {
    .name = "bcm2837-legacy",
    .for_each_pending = raspi3_irq_pending,
    .mask = raspi3_irq_mask,
    .unmask = raspi3_irq_unmask,
    /* eoi = NULL：位图型没有 EOI 概念 —— 清设备状态（W1C）就算完成一次中断。
     * set_priority/set_type/set_affinity 也留 NULL（框架调用前判空）。 */
};

void timer_init(int hz) {
  int cpu = cpu_get_id();
  kprintf("cpu %d timer init\n", cpu);

  cntfrq[cpu] = read_cntfrq() / hz;
  if (cntfrq[cpu] == 0) {
    // Avoid IRQ storm if frequency calculation underflows.
    cntfrq[cpu] = 1;
  }
  if (cpu == 0) {
    kprintf("cntfrq %d\n", cntfrq[cpu]);
  }
  write_cntv_tval(cntfrq[cpu]);
  io_write32((volatile unsigned int*)(CORE0_TIMER_IRQCNTL + 0x4 * cpu), 0x08);
  enable_cntv(1);

  /* ==================================================================
   * 接入统一中断框架（位图型 BCM2837 legacy 控制器）
   * ==================================================================
   * 【顺序】先 set_tick 再 register：反过来的话，"chip 已注册、tick 还没声明"的
   * 窗口里若有中断进来，tick 号会被当成"未注册号"而**自动 mask** ⇒ 时钟永久停掉
   * （这条坑 v3s 那边已记录，见设计文档 §11.1-3）。
   * 【为什么放这里】timer_init 由 schedule_init() 调用，此时控制台/页表都已就绪；
   * 每颗 CPU 都会走到这里，而 irq_chip_register/irq_set_tick 都是幂等的。
   * 【切换后的行为变化】中断从此走 kernel/irq.c：
   *   - tick 仍经 EX_TIMER 槽交给 do_schedule（框架会先置 ic->no = EX_TIMER）；
   *   - 未注册的号 = 计数 + 告警一次 + 自动 mask（取代老路径"一律 IRQ_DISABLE*"）；
   *   - 老 interrupt_get_source() 不再被调用（保留作为参考实现）。 */
  irq_set_tick(IRQ_RASPI3_CNTV);
  irq_chip_register(&raspi3_bcm_chip, 0, IRQ_RASPI3_IRQ_NUM);
}

void timer_end(void) {
  int cpu = cpu_get_id();
  // Always reset timer to clear interrupt
  // If tval becomes 0, the virtual timer will keep firing immediately (IRQ storm).
  if (cntfrq[cpu] != 0) {
    write_cntv_tval(cntfrq[cpu]);
  } else if (cntfrq[0] != 0) {
    write_cntv_tval(cntfrq[0]);
  } else {
    write_cntv_tval(1);
  }
}

void platform_init(void) {
  // uart_init();
  io_add_write_channel(uart_send);
}

void platform_end(void) {
}

void platform_map(void) {
  // Map entire MMIO region (MMU uses 4KB pages; page_map() maps a single page).
  // Without this, accessing offsets inside MMIO_BASE will fault (e.g. 0x3f00b898).
  for (u32 addr = (u32)MMIO_BASE; addr < (u32)(MMIO_BASE + MMIO_LENGTH);
       addr += 0x1000) {
    page_map(addr, addr, PAGE_DEV);
  }

  // Also ensure specific peripheral pages are mapped (redundant but harmless).
  page_map(((u32)UART0_DR) & ~0xfff, ((u32)UART0_DR) & ~0xfff, PAGE_DEV);
  page_map(CORE0_TIMER_IRQCNTL & ~0xfff, CORE0_TIMER_IRQCNTL & ~0xfff, PAGE_DEV);
}

int interrupt_get_source(u32 no) {
  int cpu = cpu_get_id();
  u32 src = read_core_timer_pending(cpu);

  // Local interrupt controller: bit3 is CNTVIRQ when CORE0_TIMER_IRQCNTL routes it.
  if (src & 0x08) {
    return EX_TIMER;
  }

  // Mailbox interrupts (IPI) bits are typically in [4..7]. Clear them to avoid IRQ storms.
  if (src & 0xF0) {
    ipi_clear(cpu);
    return EX_NONE;
  }

  // Any other local IRQ sources (GPU/peripheral/PMU/AXI...). If we don't decode/ack them, IRQ will storm.
  if (src & ~(0x08u | 0xF0u)) {
    u32 basic = io_read32(IRQ_BASIC_PENDING);
    u32 p1 = io_read32(IRQ_PENDING1);
    u32 p2 = io_read32(IRQ_PENDING2);

    // Rate-limited debug: helps identify which source is storming.
    static u32 storm;
    if (storm < 8 || (storm & 0x3FF) == 0) {
      u32 emmc_int = io_read32(EMMC_INTERRUPT_REG);
      kprintf("raspi3 irq storm? src=%x basic=%x p1=%x p2=%x emmc_int=%x\n",
              src, basic, p1, p2, emmc_int);
    }
    storm++;

    // If EMMC is pending, clear its interrupt status and mask it until a real driver IRQ path exists.
    if (p2 & IRQ2_EMMC_BIT) {
      // Clear any latched EMMC interrupt sources.
      io_write32(EMMC_INTERRUPT_REG, 0xFFFFFFFF);
      // Ensure it can't signal IRQ line.
      io_write32(EMMC_IRPT_EN_REG, 0);
      // Mask at the peripheral IRQ controller too (bring-up safety).
      io_write32(IRQ_DISABLE2, IRQ2_EMMC_BIT);
      return EX_NONE;
    }

    // Unknown peripheral IRQ: mask what is currently pending to avoid hard lockups.
    if (p1) io_write32(IRQ_DISABLE1, p1);
    if (p2) io_write32(IRQ_DISABLE2, p2);
    if (basic) io_write32(IRQ_BASIC_DISABLE, basic);
    return EX_NONE;
  }

  // Unknown / unhandled source.
  return EX_NONE;
}

void ipi_enable(int cpu) {
  if (cpu < 0 || cpu >= MAX_CPU) return;
  io_write32((volatile unsigned int*)(CORE0_MBOX_IRQCNTL + cpu * 4), 1);
}

void lcpu_wait_start(int cpu) {
  if (cpu <= 0 || cpu >= MAX_CPU) return;
  while (!ap_release[cpu]) {
    asm volatile("wfe" ::: "memory");
  }
  kprintf("ap %d start\n", cpu);
}

void lcpu_send_start(u32 cpu, u64 entry) {
  (void)entry;
  if (cpu >= MAX_CPU) return;
  ap_release[cpu] = 1;
  dmb();
  asm volatile("sev" ::: "memory");
}

void ipi_send(int cpu, int vec) {
  if (cpu < 0 || cpu >= MAX_CPU) return;
  io_write32((volatile unsigned int*)(CORE0_MBOX0_SET + cpu * 0x10), 1 << vec);
  dsb();
}

void ipi_clear(int cpu) {
  if (cpu < 0 || cpu >= MAX_CPU) return;
  volatile unsigned int* addr =
      (volatile unsigned int*)(CORE0_MBOX0_RDCLR + cpu * 0x10);
  u32 val = io_read32(addr);
  val = 0xFFFFFFFF;
  io_write32(addr, val);
}
