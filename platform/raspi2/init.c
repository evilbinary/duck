#include "arch/arch.h"
#include "gpio.h"
#include "libs/include/types.h"

extern boot_info_t* boot_info;
extern void dccmvac(unsigned long mva);

static void dcimvac(unsigned long mva) {
  asm volatile("mcr p15, 0, %0, c7, c6, 1" : : "r"(mva) : "memory");
}

static void io_write32(uint port, u32 data) { *(u32 *)port = data; }

static u32 io_read32(uint port) {
  u32 data;
  data = *(u32 *)port;
  return data;
}

static u32 cntfrq[MAX_CPU] = {
    0,
};

void uart_send(unsigned int c) {
  while (io_read32(UART0_FR) & 0x20) {
  }
  io_write32(UART0_DR, c);
}

unsigned int uart_receive() {
  unsigned int c;
  while (io_read32(UART0_FR) & 0x10) {
  }
  c = io_read32(UART0_DR);
  return c;
}

u32 read_core_timer_pending(int cpu) {
  u32 tmp;
  tmp = io_read32(CORE0_IRQ_SOURCE + 4 * cpu);
  return tmp;
}

void timer_init(int hz) {
  int cpu = cpu_get_id();
  if (cpu == 0) {
    kprintf("cpu %d timer init\n", cpu);
    cntfrq[cpu] = read_cntfrq();
    cntfrq[cpu] = cntfrq[cpu] / hz;
    kprintf("cntfrq %d\n", cntfrq[cpu]);
    write_cntv_tval(cntfrq[cpu]);

    u32 val = read_cntv_tval();
    kprintf("val %d\n", val);
    io_write32(CORE0_TIMER_IRQCNTL + 0x4 * cpu, 0x08);
    enable_cntv(1);
  } else {
    kprintf("ap %d timer attach\n", cpu);
    cntfrq[cpu] = read_cntfrq();
    cntfrq[cpu] = cntfrq[cpu] / hz;
    write_cntv_tval(cntfrq[cpu]);
    io_write32(CORE0_TIMER_IRQCNTL + 0x4 * cpu, 0x08);
    enable_cntv(1);
  }
}

void timer_end() {
  int cpu = cpu_get_id();
  u32 pending = read_core_timer_pending(cpu);
  if (pending & INT_SRC_TIMER3) {
    write_cntv_tval(cntfrq[cpu]);
  }
}

void platform_init() { io_add_write_channel(uart_send); }

void platform_end() {
  
}

void platform_map(){
  page_map(MMIO_BASE, MMIO_BASE, 0);
  page_map(UART0_DR, UART0_DR, 0);
  page_map(CORE0_TIMER_IRQCNTL & ~0xfff, CORE0_TIMER_IRQCNTL & ~0xfff, 0);
}

int interrupt_get_source(u32 no) {
  int cpu = cpu_get_id();
  u32 pending = read_core_timer_pending(cpu);

  if (pending & INT_SRC_TIMER3) {
    return EX_TIMER;
  }

  if (pending & (INT_SRC_MBOX0 | INT_SRC_MBOX1 | INT_SRC_MBOX2 | INT_SRC_MBOX3)) {
    return EX_IRQ;
  }

  return EX_NONE;
}

void ipi_enable(int cpu) {
  if (cpu < 0 || cpu > 4) return;
  u32 addr = CORE0_MBOX_IRQCNTL + cpu * 4;
  io_write32(addr, 1);
}

void test_smp_entry() {
  *((u32 *)0x8888) = 1 + *((u32 *)0x8888);
  for (;;) cpu_halt();
}

static volatile u32 ap_release[MAX_CPU];

void lcpu_wait_start(int cpu) {
  u32 mailbox = 3;
  u32 rdclr = CORE0_MBOX0_RDCLR + cpu * 0x10 + 4 * mailbox;

  if (cpu == 0) return;

  while (1) {
    dcimvac((unsigned long)&ap_release[cpu]);
    dsb();
    if (ap_release[cpu]) {
      break;
    }
    asm volatile("wfe");
  }
  kprintf("ap %d start\n", cpu);
  io_write32(rdclr, 0xffffffff);
  dsb();
  isb();
}

void lcpu_send_start(u32 cpu, u32 entry) {
  (void)entry;
  if (cpu < 0 || cpu > 4) return;
  u32 rdclr = CORE0_MBOX0_RDCLR + cpu * 0x10 + 4 * 3;

  io_write32(rdclr, 0xffffffff);
  dmb();
  dsb();

  ap_release[cpu] = 1;
  dccmvac((unsigned long)&ap_release[cpu]);
  if (boot_info != NULL) {
    dccmvac((unsigned long)&boot_info->kernel_entry);
    dccmvac((unsigned long)boot_info);
  }
  dmb();
  dsb();
  asm volatile("sev");
}

void ipi_send(int cpu, int vec) {
  if (cpu < 0 || cpu > 4) return;
  u32 addr = CORE0_MBOX0_SET + cpu * 0x10 + 0x80;
  io_write32(addr, 1 << vec);
  dsb();
}

void ipi_clear(int cpu) {
  if (cpu < 0 || cpu > 4) return;
  u32 mailbox = 3;
  u32 addr = CORE0_MBOX0_RDCLR + cpu * 0x10 + 4 * mailbox;
  io_write32(addr, 0xffffffff);
  dmb();
  dsb();
}
