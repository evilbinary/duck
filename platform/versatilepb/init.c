#include "arch/arch.h"
#include "gpio.h"

#define VIC_BASE_ADDR 0
// #define VIC_BASE_ADDR 0xffff0000

void versatile_irq_chip_init(void);

// UART0 PL011 PrimeCell on Versatile/PB
void uart_send_char(unsigned int c) {
  while (io_read32(UART0 + UART_FLAGS) & UART_TRANSMIT)
    ;
  io_write32(UART0 + UART_DATA, c);
}

void uart_send(unsigned int c) {
  if (c == '\n') {
    uart_send_char(c);
    c = '\r';
  }
  uart_send_char(c);
}

unsigned int uart_receive() {
  unsigned int c;

  while ((io_read32(UART0 + UART_FLAGS) & UART_RECEIVE)) {
  };
  c = io_read32(UART0 + UART_DATA);

  return c;
}

void platform_init() {
  io_add_write_channel(uart_send);

}

void platform_map() {
  // map base
  kprintf("platform map\n");

  page_map(GPIO_BASE, GPIO_BASE, PAGE_DEV);
  page_map(UART0, UART0, PAGE_DEV);
  page_map(VIC_BASE_ADDR, VIC_BASE_ADDR, PAGE_DEV);
  page_map(TIMER2_BASE, TIMER2_BASE, PAGE_DEV);
  page_map(TIMER0_BASE, TIMER0_BASE, PAGE_DEV);
  page_map(TIMER3_BASE, TIMER3_BASE, PAGE_DEV);

  page_map(PIC_BASE, PIC_BASE, PAGE_DEV);
  page_map(SD_BASE, SD_BASE, PAGE_DEV);
  page_map(SIC_BASE, SIC_BASE, PAGE_DEV);

  kprintf("platform map end\n");
}

void platform_end() {}

// 0x101E2000 for Timer 0 0x101E2020 for Timer 1 0x101E3000 for Timer 2
// 0x101E3020 for Timer 3.
void timer_init(int hz) {
  kprintf("timer init %d\n", hz);

  io_write32(TIMER0_BASE + TIMER_LOAD, hz);

  u32 reg = TIMER_CTRL_PERIODIC | TIMER_CTRL_EN | TIMER_CTRL_32BIT |
            TIMER_CTRL_INTREN | TIMER_CTRL_32BIT;
  io_write32(TIMER0_BASE + TIMER_CONTROL, reg);

  u32 val = io_read32(TIMER0_BASE + TIMER_CONTROL);
  kprintf("value=>%x %x\n", val, reg);

  u32 pic = io_read32(PIC_BASE + PIC_INT_ENABLE);
  pic |= PIC_INT_TIMER0 | SIC_IRQ_VIC_BIT;
  // pic|= (1 << 4) | (1 << 5);
  io_write32(PIC_BASE + PIC_INT_ENABLE, pic);

#ifdef VERSATILE_USE_IRQ_CHIP
  versatile_irq_chip_init();
#endif
}

void timer_end() {
  // kprintf("timer end\n");
  io_write32(TIMER0_BASE + TIMER_INTCTL, 0);
}

int interrupt_get_source(u32 no) {
  pic_regs_t* pic = (pic_regs_t*)(PIC_BASE);
  sic_regs_t* sic = (pic_regs_t*)(SIC_BASE);
  if ((pic->status & PIC_INT_TIMER0) != 0) {
    no = EX_TIMER;
  } else {
    /* 【SIC 位号≠IRQ 号】ISR_MOUSE(0x2c)/ISR_KEYBOARD(0x21) 是 PC 风格的
     * "中断向量"编码（0x20+IRQ），当寄存器位用会变成 1<<44 / 1<<33 —— 在 32 位
     * 寄存器上恒为 0，于是 EX_MOUSE / EX_KEYBOARD 永远派发不到（实测症状：
     * pl050 鼠标中断源已使能，但 mouse_handler 从不执行 ⇒ 应用收不到鼠标事件）。
     * 本平台 PL050 走 SiC：KMI0(键盘)=bit3、KMI1(鼠标)=bit4（与
     * modules/keyboard/pl050.c、modules/mouse/pl050.c 里使能的位一致）。 */
    if (sic->status & (1 << 4)) { /* KMI1 = mouse */
      no = EX_MOUSE;
    } else if (sic->status & (1 << 3)) { /* KMI0 = keyboard */
      // kprintf("get source keyboard \n");
      no = EX_KEYBOARD;
    }
  }
  return no;
}

void ipi_enable(int cpu) {}

void lcpu_send_start(u32 cpu, u32 entry) {}