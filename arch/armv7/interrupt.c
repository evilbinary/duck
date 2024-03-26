/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/

#include "../interrupt.h"

#include "context.h"
#include "cpu.h"
#include "gpio.h"

extern boot_info_t* boot_info;

interrupt_handler_t* interrutp_handlers[IDT_NUMBER];
u32 idt[IDT_NUMBER] __attribute__((aligned(32)));

void interrupt_init() {
  kprintf("interrupt init\n");
  boot_info->idt_base = idt;
  boot_info->idt_number = IDT_NUMBER;
  for (int i = 0; i < boot_info->idt_number; i++) {
    interrutp_set(i);
  }
  u32 val = idt;

  SCB->VTOR = val;
}

void interrupt_regist(u32 vec, interrupt_handler_t handler) {
  interrutp_handlers[vec] = handler;
  interrutp_set(vec);
}

void interrutp_set(int i) {
  u32 base = (u32)interrutp_handlers[i];
  idt[i] = base;
}

INTERRUPT_SERVICE
void reset_handler() {
  interrupt_entering_code(EX_RESET, 0);
  interrupt_process(interrupt_default_handler);
  cpu_halt();
}

INTERRUPT_SERVICE
void svc_handler() {
  interrupt_entering_code(EX_SYS_CALL, 0);
  interrupt_process(interrupt_default_handler);
  interrupt_exit();
}

INTERRUPT_SERVICE
void irq_handler() {
  interrupt_entering_code(EX_IRQ, 0);
  interrupt_process(interrupt_default_handler);
  interrupt_exit();
}

INTERRUPT_SERVICE
void sys_tick_handler() {
  interrupt_entering_code(EX_TIMER, 0);
  interrupt_process(interrupt_default_handler);
  interrupt_exit_ret();
}

INTERRUPT_SERVICE
void sys_pendsv_handler() {
  interrupt_entering_code(EX_SYS_CALL, 0);
  interrupt_process(interrupt_default_handler);
  interrupt_exit();
}


INTERRUPT_SERVICE
void hard_fault_handler() {
  interrupt_entering_code(EX_UNDEF, 0);
  interrupt_process(interrupt_default_handler);
  interrupt_exit();
}

INTERRUPT_SERVICE
void bus_handler() {
  interrupt_entering_code(EX_DATA_FAULT, 0);
  interrupt_process(interrupt_default_handler);
  interrupt_exit();
}

INTERRUPT_SERVICE
void usage_fault_handler() {
  interrupt_entering_code(EX_UNDEF, 0);
  interrupt_process(interrupt_default_handler);
  interrupt_exit();
}

void exception_info(interrupt_context_t* ic) {
  static const char* exception_msg[] = {
      "NONE", "RESET", "NONE", "NONE", "NONE", "NONE", "NONE",       "NONE",
      "NONE", "NONE",  "NONE", "SVC",  "NONE", "NONE", "SYS PENDSV", "SYS TICK",
  };
  int cpu = cpu_get_id();
  if (ic->no < sizeof exception_msg) {
    kprintf("exception cpu %d no %d: %s\n----------------------------\n", cpu,
            ic->no, exception_msg[ic->no]);
  } else {
    kprintf("exception cpu %d no %d:\n----------------------------\n", cpu,
            ic->no);
  }
}

void interrupt_regist_all() {
  interrupt_regist(1, reset_handler);  // reset
  interrupt_regist(2, irq_handler);
  interrupt_regist(3, hard_fault_handler);

  interrupt_regist(4, bus_handler);
  interrupt_regist(5, bus_handler);  //
  interrupt_regist(6, usage_fault_handler);

  interrupt_regist(11, svc_handler);         // svc_handler
  interrupt_regist(14, sys_pendsv_handler);  // sys_pendsv_handler
  interrupt_regist(15, sys_tick_handler);    // sys_tick_handler
}