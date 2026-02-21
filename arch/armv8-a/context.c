/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "context.h"
#include "cpu.h"
#include "arch/cpu.h"

extern boot_info_t* boot_info;

int context_get_mode(context_t* context) {
  int mode = 0;
  if (context != NULL) {
    interrupt_context_t* c = context->ksp;
    if (c != NULL) {
      // EL0 (user mode): PSTATE bits [3:0] == 0x0
      if ((c->psr & 0xF) == 0x0) {
        return 3;
      }
    }
  }
  return mode;
}

// context_init: mirrors armv7-a design exactly.
//   ic is placed at ksp_top - sizeof(interrupt_context_t) * 2
//   so there is one slot of space below it for context_switch to use ++ksp.
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

  context->eip = entry;
  context->level = level;

  // PSTATE: EL1h (0x5) with D/A/I/F masked = 0x3C5  → kernel thread
  //         EL0t (0x0) with no masks               → user thread
  u64 pstate;
  if (level == 0) {
    // kernel mode: EL1h, all interrupts masked
    pstate = 0x3C5;
  } else if (level == 3) {
    // user mode: EL0t
    pstate = 0x0;
  } else {
    kprintf("context_init: unsupported level %d\n", level);
    pstate = 0x3C5;
  }

  // ARM64 SP must be 16-byte aligned. Round ksp_top down before placing ic.
  u64 aligned_top = ksp_top & ~(u64)0xF;

  // Place ic one slot below aligned_top.
  // context_switch copies in/out of this fixed slot; no ++/-- pointer games.
  // interrupt_exit_context pops from ic → SP_EL1 ends up at aligned_top,
  // and the thread's runtime stack grows down from there.
  interrupt_context_t* ic =
      (interrupt_context_t*)(aligned_top - sizeof(interrupt_context_t));

  kmemset(ic, 0, sizeof(interrupt_context_t));
  ic->lr  = (u64)entry;
  ic->pc  = ic->lr;
  ic->psr = pstate;
  // SP_EL0: kernel threads never drop to EL0, use ksp_end as a safe
  // non-zero value so that if SP_EL0 is ever read it won't fault.
  // User threads use their own user stack top.
  ic->sp  = (level == KERNEL_MODE) ? aligned_top : (u64)usp_top;

  context->usp  = (u64)usp_top;
  context->ksp  = ic;
  context->ic   = ic;

  return 0;
}

void context_dump(context_t* c) {
  kprintf("tid: %8x\n", c->tid);
  kprintf("eip: %8lx\n", c->eip);
  kprintf("ksp: %8lx\n", (u64)c->ksp);
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
    log_error("src ksp_start is null\n");
    return -1;
  }
  if (des->ksp_start == 0) {
    log_error("des ksp_start is null\n");
    return -1;
  }

  des->usp      = src->usp;
  des->eip      = src->eip;
  des->level    = src->level;
  des->usp_size = src->usp_size;
  des->ic       = src->ic;

  // Copy entire kernel stack
  kmemmove((void*)des->ksp_start, (void*)src->ksp_start, src->ksp_size);

  // Recalculate ksp in destination with same offset from ksp_end
  u64 offset = src->ksp_end - (u64)src->ksp;
  interrupt_context_t* ic = (interrupt_context_t*)(des->ksp_end - offset);
  des->ksp = ic;

  return 0;
}

// context_switch: save current ic to current->ksp (fixed slot), load next's.
// ARM64 kstack is large; we must NOT use ++/-- pointer arithmetic because the
// slots adjacent to ic are inside the thread's runtime stack and will be
// overwritten by normal function calls.  Instead each thread has exactly one
// fixed save slot (the ic pointer set by context_init) and we copy in/out of
// that fixed slot directly, returning the original ic address so
// interrupt_exit_ret()'s "mov sp, x0" sets SP_EL1 back to that fixed address.
interrupt_context_t* context_switch(interrupt_context_t* ic, context_t* current,
                                    context_t* next) {
  if (ic == NULL || current == next) {
    return ic;
  }
  current->ic = ic;

  // Save current thread's register state into its fixed save slot.
  kmemcpy(current->ksp, ic, sizeof(interrupt_context_t));

  // Restore next thread's register state from its fixed save slot into ic
  // (which is the live ic on the current SP_EL1 stack).
  kmemcpy(ic, next->ksp, sizeof(interrupt_context_t));

  return ic;
}

void context_switch_page(context_t* context, u64 page_table) {
  cpu_set_page(page_table);
  cpu_invalid_tlb();
  cp15_invalidate_icache();
  dmb();
}
