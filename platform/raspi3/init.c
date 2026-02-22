#include "arch/arch.h"
#include "gpio.h"
#include "libs/include/types.h"

static void io_write32(volatile unsigned int* port, u32 data) {
  *port = data;
}

static u32 io_read32(volatile unsigned int* port) {
  return *port;
}

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
  if (cntfrq[cpu] != 0) {
    write_cntv_tval(cntfrq[cpu]);
  } else if (cntfrq[0] != 0) {
    write_cntv_tval(cntfrq[0]);
  }
}

void platform_init(void) {
  uart_init();
  io_add_write_channel(uart_send);
}

void platform_end(void) {
}

void platform_map(void) {
  // Map MMIO region
  page_map(MMIO_BASE, MMIO_BASE, PAGE_DEV);
  page_map((u32)UART0_DR, (u32)UART0_DR, PAGE_DEV);
  page_map(CORE0_TIMER_IRQCNTL & ~0xfff, CORE0_TIMER_IRQCNTL & ~0xfff, PAGE_DEV);
}

int interrupt_get_source(u32 no) {
  no = EX_TIMER;
  return no;
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
