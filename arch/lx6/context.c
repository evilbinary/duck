/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "context.h"
#include "cpu.h"


int context_get_mode(context_t* context) {
  int mode = 0;
  return mode;
}

int context_init(context_t* context, u32* ksp_top, u32* usp_top, u32* entry,
                 u32 level, int cpu) {
  if (context == NULL) {
    return -1;
  }
  if (ksp_top == NULL) {
    log_error("ksp is null\n");
    return -1;
  }
  if (usp_top == NULL) {
    log_error("usp end is null\n");
    return -1;
  }

  context->eip = entry;
  context->level = level;

  cpsr_t cpsr;
  cpsr.val = 0;
  if (level == 0) {
    // kernel mode
    cpsr.UM = 0; //kernel vector mode
    cpsr.LINTLEVEL = 0; // interrupts disabled
    cpsr.EXCM = 0; //  exception mode
    cpsr.WOE =1; // window overflow enabled

  } else if (level == 3) {
    cpsr.UM = 1; // usermode user vector mode
    cpsr.LINTLEVEL = 0;
    cpsr.EXCM = 0; //  exception mode

  } else {
    kprintf("not suppport level %d\n", level);
  }

  interrupt_context_t* ic = (u32)ksp_top - sizeof(interrupt_context_t)*2 ;
  kmemset(ic, 0, sizeof(interrupt_context_t));
  ic->pc = entry;
  ic->ps = cpsr.val;

  ic->a2 = 0x00020002;
  ic->a3 = 0x00030003;
  ic->a4 = 0x00040004;
  ic->a5 = 0x00050006;
  ic->a6 = 0x00060006;
  ic->a7 = 0x00070007;
  ic->a8 = 0x00080008;
  ic->a9 = 0x00090009;
  ic->a10 = 0x00100010;
  ic->a11 = 0x00110011;
  ic->a12 = 0x00120012;
  ic->a13 = 0x00130013;
  ic->a14 = 0x00140014;
  ic->a15 = 0x00160015;

  ic->sp = usp_top;
  context->usp =usp_top;
  context->ksp = ic;
}


void context_dump_fault(interrupt_context_t* context, u32 fault_addr) {
  kprintf("----------------------------\n");  
  // kprintf("current pc: %x\n", read_pc());
  context_dump_interrupt(context);
  kprintf("fault: 0x%x \n", fault_addr);
  kprintf("----------------------------\n\n");
}

void context_dump(context_t* c) {
  kprintf("ip:  %x\n", c->eip);
  kprintf("sp0: %x\n", c->ksp);
  kprintf("sp:  %x\n", c->usp);

  kprintf("--interrupt context--\n");
  interrupt_context_t* context = c->ksp;
  // context_dump_interrupt(context);
}

void context_dump_interrupt(interrupt_context_t* context) {
  kprintf("lr:  %x cpsr:%x\n", context->pc, context->ps);
  kprintf("sp:  %x\n", context);
  kprintf("pc:  %x\n", context->pc);
  // kprintf("r1:  %x\n", context->a1);
  kprintf("r2:  %x\n", context->a2);
  kprintf("r3:  %x\n", context->a3);
  kprintf("r4:  %x\n", context->a4);
  kprintf("r5:  %x\n", context->a5);
  kprintf("r6:  %x\n", context->a6);
  kprintf("r7:  %x\n", context->a7);
  kprintf("r8:  %x\n", context->a8);
  kprintf("r9:  %x\n", context->a9);
  kprintf("r10: %x\n", context->a10);
}


#define DEBUG 0
interrupt_context_t* context_switch(interrupt_context_t* ic, context_t* current,
                    context_t* next_context) {
  interrupt_context_t* c = current->ksp;
#if DEBUG
  kprintf("\n=>lr:%x sp:%x lr:%x sp:%x fp:%x irq=> lr:%x sp:%x fp:%x\n",
          current->eip, current->usp, c->lr, c->sp, c->r11,
          ic->lr, ic->sp, ic->r11);
#endif
  current->ksp = ic;
  current->usp = ic->sp;
  current->eip = ic->pc;

#if DEBUG
  c = next_context->ksp;
  kprintf("  lr:%x sp:%x irq=> lr:%x sp:%x  fp:%x\n", next_context->eip,
          next_context->usp, c->lr, c->sp, c->r11);
#endif

  return next_context->ksp;

}

int context_clone(context_t* des, context_t* src) {
  if (src->ksp_start == NULL) {
    log_error("ksp top is null\n");
    return -1;
  }
  if (des->ksp_start == NULL) {
    log_error("ksp top is null\n");
    return -1;
  }
  
}