/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/

#include "arch/interrupt.h"
#include "interrupt.h"
#include "context.h"
#include "cpu.h"

extern boot_info_t* boot_info;

interrupt_handler_t interrutp_handlers[IDT_NUMBER];

// Exception vector table
// Each entry is 128 bytes (32 instructions)
// We have 16 entries: 4 exception types × 4 combinations (EL0/EL1 + AArch64/AArch32)
extern void exception_vectors(void);

void interrupt_init(void) {
  kprintf("interrupt init\n");

  // Set exception vector table
  asm volatile("msr vbar_el1, %0" : : "r"((u64)exception_vectors) : "memory");
  isb();

  // Initialize handler table
  for (int i = 0; i < IDT_NUMBER; i++) {
    interrutp_handlers[i] = NULL;
  }

  boot_info->idt_base = (void*)exception_vectors;
  boot_info->idt_number = IDT_NUMBER;
}

void interrupt_regist(u32 vec, interrupt_handler_t handler) {
  interrutp_handlers[vec] = handler;
}

// Default exception handlers
void default_handler(interrupt_context_t* ic) {
  kprintf("Unhandled exception: no=%x code=%x pc=%x\n", ic->no, ic->code, ic->pc);
  context_dump_interrupt(ic);
  cpu_halt();
}

void sync_handler(interrupt_context_t* ic) {
  u64 esr = read_esr();
  u32 ec = get_exception_class(esr);
  u64 far = read_far();

  ic->code = ec;

  switch (ec) {
    case ESR_ELx_EC_SVC64:
      // System call
      if (interrutp_handlers[0x80] != NULL) {
        interrutp_handlers[0x80](ic);
      }
      break;
    case ESR_ELx_EC_DABT_LOW:
    case ESR_ELx_EC_DABT_CUR:
      // Data abort (page fault)
      kprintf("Data abort at %lx, far=%lx\n", ic->pc, far);
      context_dump_fault(ic, far);
      if (interrutp_handlers[EX_SYNC_EL1] != NULL) {
        interrutp_handlers[EX_SYNC_EL1](ic);
      } else {
        cpu_halt();
      }
      break;
    case ESR_ELx_EC_IABT_LOW:
    case ESR_ELx_EC_IABT_CUR:
      // Instruction abort
      kprintf("Instruction abort at %lx, far=%lx\n", ic->pc, far);
      context_dump_fault(ic, far);
      cpu_halt();
      break;
    default:
      kprintf("Unknown sync exception: ec=%x esr=%lx\n", ec, esr);
      context_dump_interrupt(ic);
      cpu_halt();
  }
}

extern void* interrupt_default_handler(interrupt_context_t* ic);

void irq_handler(interrupt_context_t* ic) {
  // Call the default handler which dispatches to exception_process
  if (interrupt_default_handler != NULL) {
    interrupt_default_handler(ic);
  }
}

void fiq_handler(interrupt_context_t* ic) {
  if (interrutp_handlers[EX_FIQ_EL1] != NULL) {
    interrutp_handlers[EX_FIQ_EL1](ic);
  } else {
    kprintf("Unhandled FIQ\n");
  }
}

void serror_handler(interrupt_context_t* ic) {
  kprintf("SError exception at pc=%x\n", ic->pc);
  context_dump_interrupt(ic);
  cpu_halt();
}

void exception_info(interrupt_context_t* ic) {
  kprintf("Exception: no=%d code=%d pc=%x psr=%x\n",
          ic->no, ic->code, ic->pc, ic->psr);
}

void interrupt_regist_all(void) {
  // Register default handlers
  // Specific handlers can be registered later
}
