#include "arch/arch.h"
#include "gpio.h"
#include "kernel/page.h"
#include "libs/include/types.h"

static void io_write32(volatile unsigned int* port, u32 data);
static u32 io_read32(volatile unsigned int* port);
void ipi_clear(int cpu);

static void io_write32(volatile unsigned int* port, u32 data) {
  *port = data;
}

static u32 io_read32(volatile unsigned int* port) {
  return *port;
}

static void io_write64(volatile u64 addr, u32 data) {
  *(volatile unsigned int*)addr = data;
}

static u32 io_read64(volatile u64 addr) {
  return *(volatile unsigned int*)addr;
}

// AP entry stub in boot-armv8-a.s (firmware spin-table target).
// Weak so non single-kernel builds still link (then SMP release is disabled).
extern char apu_entry __attribute__((weak));

static u64 cntfrq[MAX_CPU] = {0};

static void delay_cycles(int n) {
  for (volatile int i = 0; i < n; i++) {
  }
}

// Raspberry Pi 5 UART0 (PL011), clock is 50MHz.
static void uart_init(void) {
  // GPIO14/15 -> ALT0 (TXD0/RXD0)
  u32 r = io_read32(GPFSEL1);
  r &= ~((7u << 12) | (7u << 15));
  r |= (4u << 12) | (4u << 15);
  io_write32(GPFSEL1, r);

  // Pi 5: no GPPUD/GPPUDCLK. Pin 14/15 pull control lives in
  // GPIO_PUP_PDN_CNTRL_REG0 bits [29:28]/[31:30], 00 = no pull.
  u32 pup = io_read32(GPIO_PUP_PDN_CNTRL_REG0);
  pup &= ~((3u << 28) | (3u << 30));
  io_write32(GPIO_PUP_PDN_CNTRL_REG0, pup);
  delay_cycles(1500);

  io_write32(UART0_ICR, 0x7FF);  // clear pending UART interrupts

  // 50MHz UARTCLK -> 115200 baud: bauddiv = 50e6/(16*115200) = 27.1267
  io_write32(UART0_CR, 0);
  io_write32(UART0_IBRD, 27);
  io_write32(UART0_FBRD, 8);
  io_write32(UART0_LCRH, (3u << 5) | (1u << 4));  // 8N1, FIFO enable

  io_write32(UART0_IMSC, 0);  // mask all UART interrupts (polling)

  // Enable UART, TX and RX.
  io_write32(UART0_CR, (1u << 0) | (1u << 8) | (1u << 9));

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
  // Pi 5 has no BCM2711 core irq source register.
  // NOTE: do NOT read GICC_IAR here - reading it acknowledges an interrupt
  // and would steal it from the normal dispatch path.
  return 0;
}

// Enable the GIC distributor once and the CPU interface per-core.
static void gic_cpu_init(int cpu) {
  if (cpu == 0) {
    io_write64(GICD_BASE + 0x000, 1);  // GICD_CTLR: enable group 0
  }
  io_write64(GICC_BASE + 0x004, 0xFF);  // GICC_PMR: no priority masking
  io_write64(GICC_BASE + 0x000, 1);     // GICC_CTLR: enable

  // Enable virtual timer PPI (banked in GICD_ISENABLER0 for SGI/PPI)
  io_write64(GICD_BASE + 0x100 + 4 * (GIC_PPI_CNTVIRQ / 32),
             1u << (GIC_PPI_CNTVIRQ % 32));

  // Give PPI27 a reasonable priority (banked GICD_IPRIORITYRn)
  volatile u64 ipr = GICD_BASE + 0x400 + 4 * (GIC_PPI_CNTVIRQ / 4);
  u32 shift = 8 * (GIC_PPI_CNTVIRQ % 4);
  u32 v = io_read64(ipr) & ~(0xFFu << shift);
  io_write64(ipr, v | (0xA0u << shift));
}

void timer_init(int hz) {
  int cpu = cpu_get_id();
  kprintf("cpu %d timer init\n", cpu);

  cntfrq[cpu] = read_cntfrq() / hz;
  if (cntfrq[cpu] == 0) {
    cntfrq[cpu] = 1;
  }
  if (cpu == 0) {
    kprintf("cntfrq %d\n", cntfrq[cpu]);
  }
  gic_cpu_init(cpu);
  write_cntv_tval(cntfrq[cpu]);
  enable_cntv(1);
}

void timer_end(void) {
  int cpu = cpu_get_id();
  // Always reset timer to clear interrupt, avoids IRQ storm.
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

void platform_end(void) {}

void platform_map(void) {
  // Map entire legacy MMIO region (4KB pages).
  for (u32 addr = (u32)MMIO_BASE; addr < (u32)(MMIO_BASE + MMIO_LENGTH);
       addr += 0x1000) {
    page_map(addr, addr, PAGE_DEV);
  }

  // GIC-400 distributor and cpu interface pages (above 32-bit region)
  page_map((vaddr_t)GICD_BASE & ~0xFFFULL, (vaddr_t)GICD_BASE & ~0xFFFULL,
           PAGE_DEV);
  page_map((vaddr_t)GICC_BASE & ~0xFFFULL, (vaddr_t)GICC_BASE & ~0xFFFULL,
           PAGE_DEV);
}

int interrupt_get_source(u32 no) {
  u32 iar = io_read64(GICC_BASE + 0x00C);
  u32 id = iar & 0x3FF;

  if (id == GIC_SPURIOUS_ID) {
    return EX_NONE;
  }

  // Acknowledge / end the interrupt before dispatch.
  io_write64(GICC_BASE + 0x010, iar);

  if (id == GIC_PPI_CNTVIRQ) {
    return EX_TIMER;
  }

  // SGIs (0-15) are used for IPI.
  if (id < 16) {
    return EX_IRQ;
  }

  // Unknown SPI: already EOIed, ignore to avoid storms until a real
  // driver handles it.
  return EX_NONE;
}

void ipi_enable(int cpu) {
  if (cpu < 0 || cpu >= MAX_CPU) return;
  // SGI0 is used for IPI (GICD_ISENABLER0 is banked per cpu)
  io_write64(GICD_BASE + 0x100, 1u << 0);
}

// Pi 5 firmware parks secondary cores polling a spin-table at 0xd8+8*cpu.
// Writing the AP entry address there releases the core.
void lcpu_send_start(u32 cpu, u64 entry) {
  (void)entry;
  if (cpu <= 0 || cpu >= MAX_CPU) return;
  volatile u64* rel = (volatile u64*)(SPIN_RELEASE_BASE + 8 * cpu);
  *rel = (u64)&apu_entry;
  dsb();
  asm volatile("sev" ::: "memory");
}

void lcpu_wait_start(int cpu) {
  // APs enter via the firmware spin-table directly into apu_entry,
  // no in-kernel wait loop is needed on Pi 5.
}

void ipi_send(int cpu, int vec) {
  if (cpu < 0 || cpu >= MAX_CPU) return;
  // GICD_SGIR: filter=0 (target list), cpu mask, sgi id
  u32 val = (1u << 16) << cpu | (vec & 0xF);
  io_write64(GICD_BASE + 0xF00, val);
  dsb();
}

void ipi_clear(int cpu) {
  if (cpu < 0 || cpu >= MAX_CPU) return;
  // Read + EOI pending SGI to clear it.
  u32 iar = io_read64(GICC_BASE + 0x00C);
  if ((iar & 0x3FF) != GIC_SPURIOUS_ID) {
    io_write64(GICC_BASE + 0x010, iar);
  }
}
