/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/

#include "../interrupt.h"

#include "../lock.h"
#include "context.h"
#include "cpu.h"

extern boot_info_t* boot_info;

interrupt_handler_t* interrutp_handlers[IDT_NUMBER];
u32 idt[IDT_NUMBER * 2] __attribute__((aligned(32)));

void interrupt_init(int cpu) {
  kprintf("interrupt init cpu %d\n", cpu);
  if (cpu == 0) {
    boot_info->idt_base = idt;
    boot_info->idt_number = IDT_NUMBER;
    for (int i = 0; i < boot_info->idt_number; i++) {
      interrutp_set(i);
    }
  }
  asm volatile("mcr p15, 0, %0, c12, c0, 0" ::"r"(idt));
}

void interrupt_regist(u32 vec, interrupt_handler_t handler) {
  interrutp_handlers[vec] = handler;
  interrutp_set(vec);
}

void interrutp_set(int i) {
  idt[i] = 0xe59ff000 +
           (IDT_NUMBER - 2) * 4;  // ldr	pc, [pc, #24] 0x24=36=4*8=32+4
  u32 base = (u32)interrutp_handlers[i];
  idt[i + IDT_NUMBER] = base;
}

INTERRUPT_SERVICE
void reset_handler() {
  interrupt_entering_code(EX_RESET, 0, 0);
  interrupt_process(interrupt_default_handler);
  cpu_halt();
}

INTERRUPT_SERVICE
void undefined_handler() {
  interrupt_entering_code(EX_UNDEF, 0, 4);
  interrupt_process(interrupt_default_handler);
  interrupt_exit();
  // cpu_halt();
}

INTERRUPT_SERVICE
void svc_handler() {
  // asm("msr   cpsr, #0xD3 \n");
  interrupt_entering_code(EX_SYS_CALL, 0, 0);
  interrupt_process(interrupt_default_handler);
  interrupt_exit();
}

INTERRUPT_SERVICE
void pref_abort_handler() {
  interrupt_entering_code(EX_PREF_ABORT, 0, 4);
  interrupt_process(interrupt_default_handler);
  interrupt_exit();
}

INTERRUPT_SERVICE
void data_abort_handler() {
  interrupt_entering_code(EX_DATA_FAULT, 0, 8);
  interrupt_process(interrupt_default_handler);
  interrupt_exit();
}

INTERRUPT_SERVICE
void unuse_handler() {
  interrupt_entering_code(EX_OTHER, 0, 4);
  interrupt_process(interrupt_default_handler);
  interrupt_exit();
}

INTERRUPT_SERVICE
void irq_handler() {
  // asm("msr   cpsr, #0xD2 \n");
  interrupt_entering_code(EX_TIMER, 0, 4);
  interrupt_process(interrupt_default_handler);
  interrupt_exit_ret();
}

INTERRUPT_SERVICE
void fiq_handler() {
  interrupt_entering_code(EX_OTHER, 0, 4);
  interrupt_process(interrupt_default_handler);
  interrupt_exit();
}

void exception_info(interrupt_context_t* ic) {
  // static const char* exception_msg[] = {"RESET",      "UNDEFINED",  "SVC",
  //                                       "PREF ABORT", "DATA ABORT", "NOT USE",
  //                                       "IRQ",        "FIQ"};
  static const char* exception_msg[] = {"","RESET",      "DATA FAULT",  "SYS CALL",
                                        "TIMER", "UNDEF", "OTHER",
                                        "PREF ABORT",        "PERMISSION"};
  int cpu = cpu_get_id();
  if (ic->no < sizeof exception_msg) {
    kprintf("exception cpu %d no %d: %s\n", cpu, ic->no, exception_msg[ic->no]);
  } else {
    kprintf("exception cpu %d no %d:\n", cpu, ic->no);
  }
  // u32 instruction = *((u32*)ic->pc);
  // kprintf("pc: %x data: %x\n", ic->pc,instruction );

  kprintf("ifsr: %x dfsr: %x dfar: %x\n", read_ifsr(), read_dfsr(),
          read_dfar());
  context_dump_interrupt(ic);
}

void interrupt_regist_all() {
  interrupt_regist(0, reset_handler);       // reset
  interrupt_regist(1, undefined_handler);   // undefined
  interrupt_regist(2, svc_handler);         // svc
  interrupt_regist(3, pref_abort_handler);  // pref abort
  interrupt_regist(4, data_abort_handler);  // data abort
  interrupt_regist(5, unuse_handler);       // not use
  interrupt_regist(6, irq_handler);         // irq
  interrupt_regist(7, fiq_handler);         // fiq
}