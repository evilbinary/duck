#include "gpio.h"
#include "arch/arch.h"
#include "hal/clk_gate_ll.h"
#include "hal/timer_ll.h"
#include "soc/timer_group_struct.h"


#define XCHAL_TIMER0_INTERRUPT 6  /* CCOMPARE0 */
#define XCHAL_TIMER1_INTERRUPT 15 /* CCOMPARE1 */
#define XCHAL_TIMER2_INTERRUPT 16 /* CCOMPARE2 */

#define XT_TICK_PER_SEC 1000
#define XT_CLOCK_FREQ 50000000


void uart_send_char(char c) {
  while ((io_read32(UART0_STATUS) >>16 )  >= 128);
  io_write32(UART0_FIFO, c);
}

void uart_send(unsigned int c) {
  if (c == '\n') {
    uart_send_char(c);
    c = '\r';
  }
  uart_send_char(c);
}

u32 timer_get_count() {
  u32 ret;
  asm volatile("rsr %0, ccount" : "=a"(ret) :);
  return ret;
}

void reset_count() { asm volatile("wsr %0, ccount; rsync" : : "a"(0)); }

u32 timer_count = 0;

void timer_init(int hz) {
  periph_ll_enable_clk_clear_rst(PERIPH_TIMG0_MODULE);

  int xt_tick_cycles = ( XT_CLOCK_FREQ / XT_TICK_PER_SEC );

  u32 count = timer_get_count();
  reset_count();
  //kprintf("count=%d\n", count);

  count += xt_tick_cycles;

  asm volatile("wsr %0, ccompare0; rsync" : : "a"(count));

  u32 ier;
  asm volatile("rsr %0, intenable" : "=a"(ier) : : "memory");
  ier = (1<< XCHAL_TIMER0_INTERRUPT);
  asm volatile("wsr %0, intenable; rsync" : : "a"(ier));

  //0x3FF5F000 +0x48 watch dog

  TIMERG0.wdtconfig0.wdt_en=0;

}

void timer_end() {
  //kprintf("timer end %d\n", timer_count++);
  reset_count();
}

void platform_init() { 
  
  cpsr_t ps;
  ps.val = cpu_read_ps();
  ps.EXCM = 0;         // normal exception mode
  ps.LINTLEVEL = 0xF;  // interrupts disabled
  ps.UM = 1;           // usermode
  ps.WOE = 1;          // window overflow enabled
  cpu_write_ps(ps);

  io_write32(UART0_INT_ENA, 0);

  io_add_write_channel(&uart_send);

 }

void platform_end() { 
  
  uart_send_char('v');

}

void platform_map() {}


int interrupt_get_source(u32 no) {
  no=EX_TIMER;
  return no;
}
