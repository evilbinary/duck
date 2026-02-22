#include "arch/arch.h"
#include "gpio.h"
#include "libs/include/types.h"

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

// BCM2835 EMMC register block base
#define BCM2835_EMMC_BASE_ADDR  (MMIO_BASE + 0x00300000)
#define EMMC_INTERRUPT_REG      ((volatile unsigned int*)(BCM2835_EMMC_BASE_ADDR + 0x30))
#define EMMC_IRPT_EN_REG        ((volatile unsigned int*)(BCM2835_EMMC_BASE_ADDR + 0x38))

static u64 cntfrq[MAX_CPU] = {0};

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
    // Ensure 8-bit mode; keep existing divisors.
    u32 lcrh = io_read32(UART0_LCRH);
    lcrh &= ~(3u << 5);
    lcrh |= (3u << 5);
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

void uart_send(unsigned int c) {
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

void timer_init(int hz) {
  int cpu = cpu_get_id();
  kprintf("cpu %d timer init\n", cpu);

  if (cpu == 0) {
    cntfrq[cpu] = read_cntfrq();
    cntfrq[cpu] = cntfrq[cpu] / hz;
    if (cntfrq[cpu] == 0) {
      // Avoid IRQ storm if frequency calculation underflows.
      cntfrq[cpu] = 1;
    }
    kprintf("cntfrq %d\n", cntfrq[cpu]);
    write_cntv_tval(cntfrq[cpu]);

    u64 val = read_cntv_tval();
    kprintf("val %d\n", val);
    io_write32((volatile unsigned int*)(CORE0_TIMER_IRQCNTL + 0x4 * cpu), 0x08);
    enable_cntv(1);
  }
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

void lcpu_send_start(u32 cpu, u64 entry) {
  if (cpu < 0 || cpu >= MAX_CPU) return;
  u32 mailbox = 3;
  io_write32((volatile unsigned int*)(CORE0_MBOX0_SET + cpu * 0x10 + 4 * mailbox), entry);
}

void ipi_send(int cpu, int vec) {
  if (cpu < 0 || cpu >= MAX_CPU) return;
  io_write32((volatile unsigned int*)(CORE0_MBOX0_SET + cpu * 0x10 + 0x80), 1 << vec);
  dsb();
}

void ipi_clear(int cpu) {
  if (cpu < 0 || cpu >= MAX_CPU) return;
  volatile unsigned int* addr = (volatile unsigned int*)(CORE0_MBOX0_RDCLR + cpu * 0x10 + 0xC0);
  u32 val = io_read32(addr);
  val = 0xFFFFFFFF;
  io_write32(addr, val);
}
