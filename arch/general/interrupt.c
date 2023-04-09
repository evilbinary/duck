/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/

#include "../interrupt.h"

#include "context.h"
#include "cpu.h"

extern boot_info_t* boot_info;

interrupt_handler_t* interrutp_handlers[IDT_NUMBER];
u32 idt[IDT_NUMBER * 2] __attribute__((aligned(32)));

void interrutp_set(int i) {
  idt[i] = 0xe59ff000 +
           (IDT_NUMBER - 2) * 4;  // ldr	pc, [pc, #24] 0x24=36=4*8=32+4
  u32 base = (u32)interrutp_handlers[i];
  idt[i + IDT_NUMBER] = base;
}

void interrupt_init() {
  kprintf("interrupt init\n");
  boot_info->idt_base = idt;
  boot_info->idt_number = IDT_NUMBER;
  for (int i = 0; i < boot_info->idt_number; i++) {
    interrutp_set(i);
  }
  u32 val = idt;
}

void interrupt_regist(u32 vec, interrupt_handler_t handler) {
  interrutp_handlers[vec] = handler;
  interrutp_set(vec);
}

void exception_info(interrupt_context_t* ic) {}



void interrupt_regist_all() {
  log_info("interrupt_regist_all\n");
  // interrupt_regist(0, reset_handler);       // reset
  // interrupt_regist(1, undefined_handler);   // undefined
  // interrupt_regist(2, svc_handler);         // svc
  // interrupt_regist(3, pref_abort_handler);  // pref abort
  // interrupt_regist(4, data_abort_handler);  // data abort
  // interrupt_regist(5, unuse_handler);       // not use
  // interrupt_regist(6, irq_handler);         // irq
  // interrupt_regist(7, fiq_handler);         // fiq
 
}