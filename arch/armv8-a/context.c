/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "context.h"
#include "cpu.h"

extern boot_info_t* boot_info;

int context_get_mode(context_t* context) {
  int mode = 0;
  if (context != NULL) {
    interrupt_context_t* c = context->ksp;
    if (c != NULL) {
      // In ARM64, check DAIF bits in PSTATE for EL0
      // EL0 user mode: lower bits indicate
      if ((c->psr & 0xF) == 0x0) {
        return 3;  // user mode (EL0)
      }
    }
  }
  return mode;
}

int context_init(context_t* context, u64 ksp_top, u64 usp_top, u64 entry,
                 u32 level, int cpu) {
  if (context == NULL) {
    return -1;
  }
  if (ksp_top == 0) {
    log_error("ksp is null\n");
    return -1;
  }
  if (usp_top == 0) {
    log_error("usp end is null\n");
    return -1;
  }
  
  // Skip verbose logging to avoid kvsprintf issues

  context->eip = entry;
  context->level = level;

  // Setup PSTATE based on level
  // PSTATE bits: D(bit9) A(bit8) I(bit7) F(bit6) mode(bits 3:0)
  // Mode: EL0t=0x0, EL1t=0x4, EL1h=0x5
  u32 pstate = 0;
  if (level == 0) {
    // Kernel mode (EL1h) with interrupts disabled
    pstate = 0x3C5;  // D=1, A=1, I=1, F=1, mode=EL1h(0x5)
  } else if (level == 3) {
    // User mode (EL0t)
    pstate = 0x0;    // EL0t
  } else {
    // Unsupported level, set to safe default
    pstate = 0x3C5;
  }

  // Place ic at ksp_top - sizeof(ic) so that after interrupt_exit_context
  // consumes all fields with [sp],#16, sp lands exactly at ksp_top (stack top).
  interrupt_context_t* ic = (interrupt_context_t*)((u64)ksp_top - sizeof(interrupt_context_t));

  kmemset(ic, 0, sizeof(interrupt_context_t));
  ic->lr = (u64)entry;
  ic->pc = ic->lr;
  ic->psr = pstate;
  ic->sp = (u64)usp_top;
  
  // Initialize general registers with debug values
  ic->x0 = 0;
  ic->x1 = 0x00010001;
  ic->x2 = 0x00020002;
  ic->x3 = 0x00030003;
  ic->x4 = 0x00040004;
  ic->x5 = 0x00050005;
  ic->x6 = 0x00060006;
  ic->x7 = 0x00070007;
  ic->x8 = 0x00080008;
  ic->x9 = 0x00090009;
  ic->x10 = 0x00100010;
  ic->x11 = 0x00110011;  // fp
  ic->x29 = 0x00110011;  // fp (x29)
  
  context->usp = (u64)usp_top;
  context->ksp = ic;
  
  // Initialize ctx->ic to point to the interrupt context
  context->ic = ic;
  
  return 0;
}

void context_dump(context_t* c) {
  kprintf("tid: %8x\n", c->tid);
  kprintf("eip: %8lx\n", c->eip);
  kprintf("ksp: %8lx\n", c->ksp);
  kprintf("usp: %8lx\n", c->usp);

  kprintf("--interrupt context--\n");
  interrupt_context_t* ic = c->ksp;
  if (ic != NULL) {
    context_dump_interrupt(ic);
  }
}

void context_dump_interrupt(interrupt_context_t* ic) {
  kprintf("pc:  %lx\n", ic->pc);
  kprintf("psr:  %lx\n", ic->psr);
  kprintf("sp:  %lx\n", ic->sp);
  kprintf("lr:  %lx\n", ic->lr);
  kprintf("x0:  %lx\n", ic->x0);
  kprintf("x1:  %lx\n", ic->x1);
  kprintf("x2:  %lx\n", ic->x2);
  kprintf("x3:  %lx\n", ic->x3);
  kprintf("x4:  %lx\n", ic->x4);
  kprintf("x5:  %lx\n", ic->x5);
  kprintf("x6:  %lx\n", ic->x6);
  kprintf("x7:  %lx\n", ic->x7);
  kprintf("x8:  %lx\n", ic->x8);
  kprintf("x9:  %lx\n", ic->x9);
  kprintf("x10: %lx\n", ic->x10);
  kprintf("x11: %lx\n", ic->x11);
  kprintf("x29(fp): %lx\n", ic->x29);
}

void context_dump_fault(interrupt_context_t* context, u64 fault_addr) {
  kprintf("----------------------------\n");
  kprintf("ESR: %lx FAR: %lx\n", read_esr(), fault_addr);
  kprintf("current pc: %lx\n", read_pc());
  context_dump_interrupt(context);
  kprintf("fault: 0x%lx \n", fault_addr);
  kprintf("----------------------------\n\n");
}

int context_clone(context_t* des, context_t* src) {
  if (src->ksp_start == 0) {
    log_error("ksp top is null\n");
    return -1;
  }
  if (des->ksp_start == 0) {
    log_error("des ksp top is null\n");
    return -1;
  }

  // Copy normal fields
  des->usp = src->usp;
  des->eip = src->eip;
  des->level = src->level;
  des->usp_size = src->usp_size;
  des->ic = src->ic;

  // Copy kernel stack
  kmemmove((void*)des->ksp_start, (void*)src->ksp_start, src->ksp_size);

  // Calculate ksp offset
  u64 offset = src->ksp_end - (u64)src->ksp;
  interrupt_context_t* ic = (interrupt_context_t*)(des->ksp_end - offset);

  des->ksp = ic;

  return 0;
}

interrupt_context_t* context_switch(interrupt_context_t* ic, context_t* current,
                                    context_t* next) {
  if (ic == NULL || current == next) {
    return ic;
  }
  // Save current thread's register state into its own ic slot
  current->ic = ic;
  kmemcpy(current->ksp, ic, sizeof(interrupt_context_t));

  // Load next thread's register state into the current exception frame
  kmemcpy(ic, next->ksp, sizeof(interrupt_context_t));

  // Stash next's ksp_end in ic->no (offset 0). interrupt_exit_ret reads it
  // to set SP_EL1 correctly before eret, since ic is NOT at ksp_end-288.
  ic->no = next->ksp_end;

  return ic;
}
