/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/

#include "arch/interrupt.h"
#include "interrupt.h"
#include "context.h"
#include "cpu.h"
#include "kernel/kernel.h"

extern boot_info_t* boot_info;

interrupt_handler_t interrutp_handlers[IDT_NUMBER];

// Exception vector table - must be 2KB aligned
// ARM64 requires 16 entries: 4 groups x 4 types (Sync/IRQ/FIQ/SError)
// Each entry is at a 0x80 (128) byte boundary
__attribute__((naked, aligned(2048)))
void exception_vectors(void) {
  __asm__ volatile(
    // Group 0: Current EL with SP0 (shouldn't happen in normal operation)
    "b exception_sp0_sync\n"       // 0x000: Sync
    ".align 7\n"
    "b .\n"                        // 0x080: IRQ
    ".align 7\n"
    "b .\n"                        // 0x100: FIQ
    ".align 7\n"
    "b .\n"                        // 0x180: SError

    // Group 1: Current EL with SPx (kernel exceptions)
    ".align 7\n"
    "b exception_current_sync\n"   // 0x200: Sync
    ".align 7\n"
    "b exception_current_irq\n"    // 0x280: IRQ
    ".align 7\n"
    "b exception_current_fiq\n"    // 0x300: FIQ
    ".align 7\n"
    "b exception_current_serror\n" // 0x380: SError

    // Group 2: Lower EL using AArch64 (user mode exceptions)
    ".align 7\n"
    "b exception_lower_sync\n"     // 0x400: Sync
    ".align 7\n"
    "b exception_lower_irq\n"      // 0x480: IRQ
    ".align 7\n"
    "b exception_lower_fiq\n"      // 0x500: FIQ
    ".align 7\n"
    "b exception_lower_serror\n"   // 0x580: SError

    // Group 3: Lower EL using AArch32 (not supported)
    ".align 7\n"
    "b .\n"                        // 0x600: Sync
    ".align 7\n"
    "b .\n"                        // 0x680: IRQ
    ".align 7\n"
    "b .\n"                        // 0x700: FIQ
    ".align 7\n"
    "b .\n"                        // 0x780: SError
  );
}

// ========================================
// Exception handlers using INTERRUPT_SERVICE
// Similar to armv7-a style
// ========================================

// SP0 exceptions (should not happen)
INTERRUPT_SERVICE
void exception_sp0_sync(void) {
  asm volatile("b .");
}

// Current EL synchronous exception (kernel mode)
INTERRUPT_SERVICE
void exception_current_sync(void) {
  interrupt_entering_code(EX_SYNC_EL1, 0, 0);
  interrupt_process(interrupt_default_handler);
  interrupt_exit();
}

// Current EL IRQ
INTERRUPT_SERVICE
void exception_current_irq(void) {
  interrupt_entering_code(EX_IRQ, 0, 0);
  interrupt_process(interrupt_default_handler);
  interrupt_exit_ret();
}

// Current EL FIQ
INTERRUPT_SERVICE
void exception_current_fiq(void) {
  interrupt_entering_code(EX_OTHER, 0, 0);
  interrupt_process(interrupt_default_handler);
  interrupt_exit();
}

// Current EL SError
INTERRUPT_SERVICE
void exception_current_serror(void) {
  interrupt_entering_code(EX_OTHER, 0, 0);
  interrupt_process(interrupt_default_handler);
  interrupt_exit();
}

// Lower EL synchronous exception (user mode -> EL1)
INTERRUPT_SERVICE
void exception_lower_sync(void) {
  interrupt_entering_code(EX_SYS_CALL, 0, 0);
  interrupt_process(interrupt_default_handler);
  interrupt_exit_ret();
}

// Lower EL IRQ
INTERRUPT_SERVICE
void exception_lower_irq(void) {
  interrupt_entering_code(EX_IRQ, 0, 0);
  interrupt_process(interrupt_default_handler);
  interrupt_exit_ret();
}

// Lower EL FIQ
INTERRUPT_SERVICE
void exception_lower_fiq(void) {
  interrupt_entering_code(EX_OTHER, 0, 0);
  interrupt_process(interrupt_default_handler);
  interrupt_exit();
}

// Lower EL SError
INTERRUPT_SERVICE
void exception_lower_serror(void) {
  interrupt_entering_code(EX_OTHER, 0, 0);
  interrupt_process(interrupt_default_handler);
  interrupt_exit();
}

// ========================================
// C functions
// ========================================

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

// Synchronous exception handler (called from interrupt_process)
void* sync_handler(interrupt_context_t* ic) {
  u64 esr = read_esr();
  u32 ec = get_exception_class(esr);
  u64 far = read_far();

  ic->code = ec;

  switch (ec) {
    case ESR_ELx_EC_SVC64:
      // System call
      if (interrutp_handlers[0x80] != NULL) {
        return interrutp_handlers[0x80](ic);
      }
      break;
    case ESR_ELx_EC_DABT_LOW:
    case ESR_ELx_EC_DABT_CUR:
      // Data abort (page fault)
      if (interrutp_handlers[EX_DATA_FAULT] != NULL) {
        return interrutp_handlers[EX_DATA_FAULT](ic);
      } else {
        kprintf("Data abort at %lx, far=%lx\n", ic->pc, far);
        context_dump_fault(ic, far);
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
  return ic;
}

extern void* interrupt_default_handler(interrupt_context_t* ic);

void* irq_handler(interrupt_context_t* ic) {
  // Call the default handler which dispatches to exception_process
  if (interrupt_default_handler != NULL) {
    return interrupt_default_handler(ic);
  }
  return ic;
}

void* fiq_handler(interrupt_context_t* ic) {
  if (interrutp_handlers[EX_FIQ_EL1] != NULL) {
    return interrutp_handlers[EX_FIQ_EL1](ic);
  } else {
    kprintf("Unhandled FIQ\n");
  }
  return ic;
}

void* serror_handler(interrupt_context_t* ic) {
  kprintf("SError exception at pc=%x\n", ic->pc);
  context_dump_interrupt(ic);
  cpu_halt();
  return ic;
}

void exception_info(interrupt_context_t* ic) {
  kprintf("Exception: no=%d code=%d pc=%x psr=%x\n",
          ic->no, ic->code, ic->pc, ic->psr);
}

void interrupt_regist_all(void) {
  // Handlers are registered via interrupt_regist
}
