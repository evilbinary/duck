/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/

#include "../interrupt.h"
#include "interrupt.h"
#include "context.h"
#include "cpu.h"

extern boot_info_t* boot_info;

interrupt_handler_t interrutp_handlers[IDT_NUMBER];


// ============================================================
// Exception vector table - 2KB aligned, entries at 0x80 steps
// ============================================================
__attribute__((naked, aligned(2048), section(".text.vectors")))
void exception_vectors(void) {
  __asm__ volatile(
    // Group 0: Current EL with SP_EL0 (EL1t uses SP_EL0)
    // Route to the same handlers as "current EL with SP_ELx" so EL1t threads
    // can use SVC/IRQ without falling into the old halt stubs.
    "b exception_sp0_sync\n"
    ".align 7\n"
    "b exception_sp0_irq\n"
    ".align 7\n"
    "b exception_sp0_fiq\n"
    ".align 7\n"
    "b .\n"

    // Group 1: Current EL with SP_ELx (kernel exceptions)
    ".align 7\n"
    "b exception_current_sync\n"   // 0x200: kernel sync (SVC / data abort)
    ".align 7\n"
    "b exception_current_irq\n"    // 0x280: kernel IRQ
    ".align 7\n"
    "b exception_current_fiq\n"    // 0x300: kernel FIQ
    ".align 7\n"
    "b .\n"                        // 0x380: SError (halt)

    // Group 2: Lower EL AArch64 (user → EL1)
    ".align 7\n"
    "b exception_lower_sync\n"     // 0x400: user sync (SVC / page fault)
    ".align 7\n"
    "b exception_lower_irq\n"      // 0x480: user IRQ
    ".align 7\n"
    "b exception_lower_fiq\n"      // 0x500: user FIQ
    ".align 7\n"
    "b .\n"                        // 0x580: SError (halt)

    // Group 3: Lower EL AArch32 (unsupported)
    ".align 7\n"
    "b .\n"
    ".align 7\n"
    "b .\n"
    ".align 7\n"
    "b .\n"
    ".align 7\n"
    "b .\n"
  );
}

// ============================================================
// Current EL with SP_EL0 (EL1t): switch to SP_EL1 stack first
// ============================================================
INTERRUPT_SERVICE
void exception_sp0_sync(void) {
  __asm__ volatile(
      "msr spsel, #1\n"
      "b exception_current_sync\n");
}

INTERRUPT_SERVICE
void exception_sp0_irq(void) {
  __asm__ volatile(
      "msr spsel, #1\n"
      "b exception_current_irq\n");
}

INTERRUPT_SERVICE
void exception_sp0_fiq(void) {
  __asm__ volatile(
      "msr spsel, #1\n"
      "b exception_current_fiq\n");
}

// ============================================================
// Current EL synchronous (kernel SVC / kernel data abort)
// Kernel threads can call schedule() via SVC (e.g. thread_yield),
// which may invoke context_switch.  Use interrupt_exit_ret() so
// SP_EL1 is reset to ic base from x0 before popping, same as IRQ.
// ============================================================
INTERRUPT_SERVICE
void exception_current_sync(void) {
  interrupt_entering_code(EX_SYS_CALL, 0, 0);
  interrupt_process(sync_handler);
  interrupt_exit_ret();
}

// ============================================================
// Current EL IRQ - may cause context switch → use exit_ret
// ============================================================
INTERRUPT_SERVICE
void exception_current_irq(void) {
  interrupt_entering_code(EX_IRQ, 0, 0);
  interrupt_process(interrupt_default_handler);
  interrupt_exit_ret();
}

// ============================================================
// Current EL FIQ
// ============================================================
INTERRUPT_SERVICE
void exception_current_fiq(void) {
  interrupt_entering_code(EX_OTHER, 0, 0);
  interrupt_process(interrupt_default_handler);
  interrupt_exit();
}

// ============================================================
// Lower EL synchronous (user SVC / user page fault)
// User syscalls go through schedule() which may call context_switch,
// replacing ic contents with next thread's registers.
// interrupt_exit_ret() does "mov sp, x0" first so SP_EL1 is reset
// to the ic base before popping — identical to armv7-a irq_handler.
// ============================================================
INTERRUPT_SERVICE
void exception_lower_sync(void) {
  interrupt_entering_code(EX_SYS_CALL, 0, 0);
  interrupt_process(sync_handler);
  interrupt_exit_ret();
}

// ============================================================
// Lower EL IRQ - may context switch → use exit_ret
// ============================================================
INTERRUPT_SERVICE
void exception_lower_irq(void) {
  interrupt_entering_code(EX_IRQ, 0, 0);
  interrupt_process(interrupt_default_handler);
  interrupt_exit_ret();
}

// ============================================================
// Lower EL FIQ
// ============================================================
INTERRUPT_SERVICE
void exception_lower_fiq(void) {
  interrupt_entering_code(EX_OTHER, 0, 0);
  interrupt_process(interrupt_default_handler);
  interrupt_exit();
}

// ============================================================
// C-level synchronous exception dispatch.
//
// Design mirrors armv7-a: just set ic->no to the right exception
// number and call interrupt_default_handler.  No ksp_end tricks.
// interrupt_exit / interrupt_exit_ret restore from sp directly
// (same thread) or from x0 (switched thread).
// ============================================================
void* sync_handler(interrupt_context_t* ic) {
  u64 esr = read_esr();
  u32 ec  = get_exception_class(esr);
  u64 far = read_far();

  ic->code = ec;

  switch (ec) {
    case ESR_ELx_EC_SVC64:
      ic->no = EX_SYS_CALL;
      break;
    case ESR_ELx_EC_DABT_LOW:
    case ESR_ELx_EC_DABT_CUR:
      ic->no = EX_DATA_FAULT;
      break;
    case ESR_ELx_EC_IABT_LOW:
    case ESR_ELx_EC_IABT_CUR:
      ic->no = EX_PREF_ABORT;
      break;
    default:
      kprintf("sync: unknown ec=%x esr=%lx pc=%lx\n", ec, esr, ic->pc);
      context_dump_interrupt(ic);
      cpu_halt();
      break;
  }

  void* ret = interrupt_default_handler(ic);
  // interrupt_default_handler / exception_process may return NULL when no
  // handler is registered.  interrupt_exit_ret() does "mov sp, x0" so a
  // NULL return would set SP_EL1 = 0 and corrupt the kernel stack on the
  // next exception.  Always return a valid ic pointer.
  return (ret != NULL) ? ret : ic;
}

// ============================================================
// C support functions
// ============================================================

void interrupt_init(void) {
  kprintf("interrupt init\n");

  asm volatile("msr vbar_el1, %0" : : "r"((u64)exception_vectors) : "memory");
  isb();

  for (int i = 0; i < IDT_NUMBER; i++) {
    interrutp_handlers[i] = NULL;
  }

  boot_info->idt_base   = (void*)exception_vectors;
  boot_info->idt_number = IDT_NUMBER;
}

void interrupt_regist(u32 vec, interrupt_handler_t handler) {
  interrutp_handlers[vec] = handler;
}

void exception_info(interrupt_context_t* ic) {
  static const char* msgs[] = {
    "", "RESET", "DATA FAULT", "SYS CALL",
    "IRQ",  "UNDEF", "OTHER", "PREF ABORT", "PERMISSION"
  };
  int cpu = cpu_get_id();
  if (ic->no < sizeof(msgs) / sizeof(msgs[0])) {
    kprintf("exception cpu %d no %d: %s\n", cpu, ic->no, msgs[ic->no]);
  } else {
    kprintf("exception cpu %d no %lld\n", cpu, ic->no);
  }
  kprintf("esr: %lx far: %lx\n", read_esr(), read_far());
  context_dump_interrupt(ic);
}

void interrupt_regist_all(void) {
  // handlers registered via interrupt_regist()
}
