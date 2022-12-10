/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/

#include "../interrupt.h"

#include "platform/platform.h"

extern boot_info_t* boot_info;

idt_entry_t idt[IDT_NUMBER];
interrupt_handler_t* interrutp_handlers[IDT_NUMBER];

void interrupt_init(int cpu) {
  boot_info->idt_base = idt;
  boot_info->idt_number = IDT_NUMBER;
  for (int i = 0; i < boot_info->idt_number; i++) {
    u64* p = &idt[i];
    *p = 0;
  }
  for (int i = 0; i < boot_info->idt_number; i++) {
    interrutp_set(i);
  }

  idt_ptr_t idt_ptr;
  idt_ptr.limit = boot_info->idt_number;
  idt_ptr.base = (u32)boot_info->idt_base;
  __asm__("lidt	%0\n\t" ::"m"(idt_ptr));

  if (cpu == 0) {
    // pic 8259
    // icw1 init state
    io_write8(0x20, 0x11);
    io_write8(0xa0, 0x11);

    // icw2 map irqs  0-7 =>0x20-0x27 8-f => 0x28-0x2f
    io_write8(0x21, 0x20);
    io_write8(0xa1, 0x28);

    // icw3  IRQ2 is slave (0000 0100)
    io_write8(0x21, 0x04);
    io_write8(0xa1, 0x02);  //(0000 0010)

    // icw4 8086mode
    io_write8(0x21, 0x01);
    io_write8(0xa1, 0x01);

    // // disable pic
    // io_write8(0x21, 0xff);
    // io_write8(0xa1, 0xff);
  }
}

void timer_init(int hz) {
  unsigned int divisor = 1193180 / hz;
  io_write8(0x43, 0x36);
  io_write8(0x40, divisor & 0xff);
  io_write8(0x40, divisor >> 8);
  io_write8(0x21, io_read8(0x21) & 0xfe);
}

void timer_end() { io_write8(0x20, 0x20); }

static inline u16 pic_get_irr() {
  u16 val = 0;
  io_write8(0x20, 0x0e);
  val = (io_read8(0xa0) << 8);

  io_write8(0xa0, 0x0e);
  val = val | io_read8(0x20);
  return (io_read8(0xa0) << 8) | io_read8(0x20);
}

static inline u16 pic_get_isr() {
  io_write8(0x20, 0x0f);  // 0x0b isr data
  io_write8(0xa0, 0x0f);
  return (io_read8(0xa0) << 8) | io_read8(0x20);
}

void interrupt_regist(u32 vec, interrupt_handler_t handler) {
  interrutp_handlers[vec] = handler;
  interrutp_set(vec);
}

void interrutp_set(int i) {
  idt[i].selector = GDT_ENTRY_32BIT_CS * GDT_SIZE;
  u32 base = (u32)interrutp_handlers[i];
  idt[i].basel = base & 0xFFFF;
  idt[i].baseh = (base >> 16) & 0xFFFF;
  idt[i].attrs = 0x8e | 0x60;
  idt[i].zero = 0;
}

INTERRUPT_SERVICE
void divide_error() {
  interrupt_entering_code(0, 0);
  interrupt_process(interrupt_default_handler);
  cpu_halt();
}
INTERRUPT_SERVICE
void debug_exception() {
  interrupt_entering_code(1, 0);
  interrupt_process(interrupt_default_handler);
  cpu_halt();
}
INTERRUPT_SERVICE
void nmi() {
  interrupt_entering_code(2, 0);
  interrupt_process(interrupt_default_handler);
  cpu_halt();
}
INTERRUPT_SERVICE
void breakpoint() {
  interrupt_entering_code(3, 0);
  interrupt_process(interrupt_default_handler);
  cpu_halt();
}
INTERRUPT_SERVICE
void overflow() {
  interrupt_entering_code(4, 0);
  interrupt_process(interrupt_default_handler);
  cpu_halt();
}
INTERRUPT_SERVICE
void bounds_check() {
  interrupt_entering_code(5, 0);
  interrupt_process(interrupt_default_handler);
  cpu_halt();
}

INTERRUPT_SERVICE
void invalid_opcode() {
  interrupt_entering_code(6, 0);
  interrupt_process(interrupt_default_handler);
  cpu_halt();
}
INTERRUPT_SERVICE
void cop_not_avalid() {
  interrupt_entering_code(7, 0);
  interrupt_process(interrupt_default_handler);
  cpu_halt();
}
INTERRUPT_SERVICE
void double_fault() {
  interrupt_entering(8);
  interrupt_process(interrupt_default_handler);
  cpu_halt();
}
INTERRUPT_SERVICE
void overrun() {
  interrupt_entering_code(9, 0);
  interrupt_process(interrupt_default_handler);
  cpu_halt();
}
INTERRUPT_SERVICE
void invalid_tss() {
  interrupt_entering(10);
  interrupt_process(interrupt_default_handler);
  cpu_halt();
}
INTERRUPT_SERVICE
void seg_not_present() {
  interrupt_entering(11);
  interrupt_process(interrupt_default_handler);
  cpu_halt();
}
INTERRUPT_SERVICE
void stack_exception() {
  interrupt_entering(12);
  interrupt_process(interrupt_default_handler);
  cpu_halt();
}
INTERRUPT_SERVICE
void general_protection() {
  interrupt_entering(13);
  interrupt_process(interrupt_default_handler);
  interrupt_exit();
}

INTERRUPT_SERVICE void page_fault() {
  interrupt_entering(14);
  interrupt_process(interrupt_default_handler);
  // cpu_halt();
  interrupt_exit();
}
INTERRUPT_SERVICE
void reversed() {
  interrupt_entering_code(15, 0);
  interrupt_process(interrupt_default_handler);
  cpu_halt();
}
INTERRUPT_SERVICE
void coprocessor_error() {
  interrupt_entering_code(16, 0);
  interrupt_process(interrupt_default_handler);
  cpu_halt();
}

void exception_info(interrupt_context_t* ic){
  static const char* exception_msg[] = {
      "DIVIDE ERROR",      "DEBUG EXCEPTION",
      "BREAKPOINT",        "NMI",
      "OVERFLOW",          "BOUNDS CHECK",
      "INVALID OPCODE",    "COPROCESSOR NOT VALID",
      "DOUBLE FAULT",      "OVERRUN",
      "INVALID TSS",       "SEGMENTATION NOT PRESENT",
      "STACK EXCEPTION",   "GENERAL PROTECTION",
      "PAGE FAULT",        "REVERSED",
      "COPROCESSOR_ERROR",
  };

  if (ic->no < sizeof exception_msg) {
    kprintf("exception cpu %d no %d: code: %d %s", cpu, ic->no,
            ic->code, exception_msg[ic->no]);
  } else {
    kprintf("interrupt cpu %d %d", cpu, ic->no);
  }
  kprintf("\n----------------------------\n");
  context_dump_interrupt(ic);
}

void interrupt_regist_all() {
  interrupt_regist(0, divide_error);
  interrupt_regist(1, debug_exception);
  interrupt_regist(2, nmi);
  interrupt_regist(3, breakpoint);
  interrupt_regist(4, overflow);
  interrupt_regist(5, bounds_check);
  interrupt_regist(6, invalid_opcode);
  interrupt_regist(7, cop_not_avalid);
  interrupt_regist(8, double_fault);
  interrupt_regist(9, overrun);
  interrupt_regist(10, invalid_tss);
  interrupt_regist(11, seg_not_present);
  interrupt_regist(12, stack_exception);
  interrupt_regist(13, general_protection);
  interrupt_regist(14, page_fault);
  interrupt_regist(15, reversed);
  interrupt_regist(16, coprocessor_error);
  interrupt_regist(17, reversed);
  interrupt_regist(18, reversed);

  // exception
  exception_regist(14, do_page_fault);
}