/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/

#include "../interrupt.h"

#include "context.h"
#include "cpu.h"

#define FN_ALIGN __attribute__((aligned(4)))

extern boot_info_t* boot_info;

interrupt_handler_t* interrutp_handlers[IDT_NUMBER];

void interrupt_init() {
  kprintf("interrupt init\n");
  for (int i = 0; i < boot_info->idt_number; i++) {
    interrutp_set(i);
  }

  extern void interrupt_handler();
  cpu_write_stvec(&interrupt_handler);
}

void interrupt_regist(u32 vec, interrupt_handler_t handler) {
  interrutp_handlers[vec] = handler;
  interrutp_set(vec);
}

void interrutp_set(int i) { u32 base = (u32)interrutp_handlers[i]; }

INTERRUPT_SERVICE
void reset_handler() {
  interrupt_entering_code(EX_RESET, 0, 0);
  interrupt_process(interrupt_default_handler);
  interrupt_exit_ret();
}

INTERRUPT_SERVICE
void timer_handler() {
  interrupt_entering_code(EX_TIMER, 0, 0);
  interrupt_process(interrupt_default_handler);
  interrupt_exit_ret();
}

void* interrupt_handler_process(interrupt_context_t* ic) {
   static const char* intr_desc[16] = {
      [0] "user software interrupt",
      [1] "supervisor software interrupt",
      [2] "<reserved for future standard use>",
      [3] "<reserved for future standard use>",
      [4] "user timer interrupt",
      [5] "supervisor timer interrupt",
      [6] "<reserved for future standard use>",
      [7] "<reserved for future standard use>",
      [8] "user external interrupt",
      [9] "supervisor external interrupt",
      [10] "<reserved for future standard use>",
      [11] "<reserved for future standard use>",
      [12] "<reserved for future standard use>",
      [13] "<reserved for future standard use>",
      [14] "<reserved for future standard use>",
      [15] "<reserved for future standard use>",
  };
  static const char* nointr_desc[16] = {
      [0] "instruction address misaligned",
      [1] "instruction access fault",
      [2] "illegal instruction",
      [3] "breakpoint",
      [4] "load address misaligned",
      [5] "load access fault",
      [6] "store/AMO address misaligned",
      [7] "store/AMO access fault",
      [8] "environment call from U-mode",
      [9] "environment call from S-mode",
      [10] "<reserved for future standard use>",
      [11] "<reserved for future standard use>",
      [12] "instruction page fault",
      [13] "load page fault",
      [14] "<reserved for future standard use>",
      [15] "store/AMO page fault",
  };

  u32 cause = cpu_read_scause();
  u32 interrupt = cause & 0x80000000;
  u32 code = cause & ~0x80000000;

  ic->code = code;

  if (interrupt) {
    // log_debug("inc %s\n", intr_desc[code]);
    switch (code) {
      case 0:
        // ic->no = EX_SYS_CALL;
        // interrupt_default_handler(ic);
        break;
      case 1:  // Supervisor software interrupt
        ic->no = EX_SYS_CALL;
        interrupt_default_handler(ic);
        break;
      case 5:  // Supervisor timer interrupt
        ic->no = EX_TIMER;
        interrupt_default_handler(ic);
        break;

      case 9:  // Supervisor external interrupt
        break;
      case 15:  // data abort
        ic->no = EX_DATA_FAULT;
        interrupt_default_handler(ic);
        break;
      default:
        interrupt_default_handler(ic);
        break;
    }
  } else {
    log_debug("interrupt %d code %d sepc %x\n", interrupt, code, ic->sepc);
    log_debug("noinc %s\n", nointr_desc[code]);
  }
}

FN_ALIGN
INTERRUPT_SERVICE
void interrupt_handler() {
  interrupt_entering_code(EX_RESET, 0, 0);
  interrupt_process(interrupt_handler_process);
  interrupt_exit_ret();
}

void exception_info(interrupt_context_t* ic) {}

void interrupt_regist_all() {
  // interrupt_regist(0, reset_handler);  // reset
  // interrupt_regist(1, undefined_handler);   // undefined
  // interrupt_regist(2, svc_handler);         // svc
  // interrupt_regist(3, pref_abort_handler);  // pref abort
  // interrupt_regist(4, data_abort_handler);  // data abort
  // interrupt_regist(5, unuse_handler);       // not use
  // interrupt_regist(6, irq_handler);         // irq
  // interrupt_regist(7, timer_handler);  // fiq
}