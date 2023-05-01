#include "init.h"

#include "arch/cpu.h"
#include "arch/interrupt.h"
#include "arch/pmemory.h"
#include "gpio.h"

#define FN_ALIGN __attribute__((aligned(4)))

#ifdef SBI
#include "sbi.h"
#endif

static void io_write32(uint port, u32 data) { *(u32 *)port = data; }

static u32 io_read32(uint port) {
  u32 data;
  data = *(u32 *)port;
  return data;
}

static void io_write8(uint port, u8 data) { *(u8 *)port = data; }

static u8 io_read8(uint port) {
  u8 data;
  data = *(u8 *)port;
  return data;
}

void uart_send_char(unsigned int c) {
  while ((io_read8(UART_BASE + REG_LSR) & (1 << 5)) == 0)
    ;
  io_write32(UART_BASE + REG_THR, c);
}

void uart_send(unsigned int c) {
  if (c == '\n') {
    uart_send_char(c);
    c = '\r';
  }
  uart_send_char(c);
}

unsigned int uart_receive() {
  char c;
  c = io_read8(UART_BASE + REG_LSR);

  if (c & 1) {
    return c;
  }
  return -1;
}

int timer_val = 0;

void mtimer_end() {
  int id = cpu_get_id();
  *(u32 *)CLINT_MTIMECMP(id) = *(u32 *)CLINT_MTIME + timer_val;
  // cpu_write_mstatus(cpu_read_mstatus() & ~MSTATUS_MIE);
  // cpu_write_mie(cpu_read_mie() & ~MIE_MTIE);

  // *(u32 *)CLINT_MSIP(id) = 3;

  log_debug("mtimer end\n");
}

FN_ALIGN
INTERRUPT_SERVICE
void mtimer_handler() {
  asm("csrrw a0, mscratch, a0\n");
  asm("sw a1, 0(a0)\n"
      "sw a2, 8(a0)\n"
      "sw a3, 16(a0)\n");
  mtimer_end();
  // log_debug("mtimer_handler\n");

  asm("li a1, 2\n");
  asm("csrw sip, a1\n");

  asm(" lw a3, 16(a0)\n"
      " lw a2, 8(a0)\n"
      " lw a1, 0(a0)\n");

  asm("csrrw a0, mscratch, a0\n");
  asm("mret\n");
}

void timer_init(int hz) {
  log_debug("timer init %d\n", hz);
  int id = cpu_get_id();
  timer_val = hz;

#ifdef SBI

  sbi_set_timer(cpu_csr_read(CSR_TIME) + timer_val);
  cpu_write_sie(cpu_read_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);
  // cpu_write_sstatus(cpu_read_sstatus() | SSTATUS_SIE);

#else
  cpu_write_medeleg(0xffff);
  cpu_write_mideleg(0xffff);

#ifdef USE_MMODE_TIMER
  *(u32 *)CLINT_MTIMECMP(id) = *(u32 *)CLINT_MTIME + timer_val;  //
  *(u32 *)CLINT_MSIP(id) = 1;

  cpu_write_mtvec(&mtimer_handler);
  cpu_write_mie(cpu_read_mie() | MIE_MTIE);
  cpu_write_sie(cpu_read_sie() | SIE_STIE);

  // cpu_write_mstatus(cpu_read_mstatus() | MSTATUS_MIE);
  // cpu_write_sie(cpu_read_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);
#endif

#ifdef USE_SMODE_TIMER
  cpu_write_stimecmp(*(u32 *)CLINT_MTIME + timer_val);
  cpu_write_sie(cpu_read_sie() | SIE_STIE);
  // cpu_write_sstatus(cpu_read_sstatus() | SSTATUS_SIE);
#endif

#endif
}

void timer_end() {
  int id = cpu_get_id();

#ifdef SBI
  sbi_set_timer(cpu_csr_read(CSR_TIME) + timer_val);
#else
#ifdef USE_MMODE_TIMER
  cpu_write_sip(cpu_read_sip() & ~2);
#endif

#ifdef USE_SMODE_TIMER
  // cpu_csr_write(CSR_TIME, cpu_csr_read(CSR_TIME) +timer_val);
  // cpu_write_sip(*(u32 *)CLINT_MTIME + timer_val);
  // cpu_write_sip(cpu_read_sip() & ~2);
  // cpu_write_sip(cpu_read_sip() & ~(1<<1 ) );
  // cpu_write_sie(cpu_read_sie() & ~(SIE_STIE));
  // asm("csrc sip, %0" ::"r"(1 << 5));
  *(u32 *)CLINT_MTIMECMP(id) = *(u32 *)CLINT_MTIME + timer_val;

#endif

#endif
  log_debug("timer end\n");
}

void platform_init() { io_add_write_channel(uart_send); }

void platform_end() {}

void platform_map() {}

void ipi_enable(int cpu) {}

void lcpu_send_start(u32 cpu, u32 entry) {}

void ipi_send(int cpu, int vec) {}

void ipi_clear(int cpu) {}