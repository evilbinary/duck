/*******************************************************************
 * Copyright 2021-2080 evilbinary
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
  context->ksp = ksp_top;
  context->usp = usp_top;

  cpsr_t cpsr;
  cpsr.val = 0;
  if (level == 0) {
    // kernel mode
    cpsr.UM = 0;
    cpsr.LINTLEVEL = 0;
    cpsr.EXCM = 0;
    interrupt_context_t* c = ksp_top;
  } else if (level == 3) {
    cpsr.UM = 1;
    cpsr.LINTLEVEL = 3;
    cpsr.EXCM = 0;

  } else {
    kprintf("not suppport level %d\n", level);
  }

  interrupt_context_t* user = ksp_top;
  kmemset(user, 0, sizeof(interrupt_context_t));
  user->pc = entry;
  user->ps = cpsr.val;

  user->a0 = 0x00000000;
  // user->a1 = 0x00010001;
  user->a2 = 0x00020002;
  user->a3 = 0x00030003;
  user->a4 = 0x00040004;
  user->a5 = 0x00050006;
  user->a6 = 0x00060006;
  user->a7 = 0x00070007;
  user->a8 = 0x00080008;
  user->a9 = 0x00090009;
  user->a10 = 0x00100010;
  user->a11 = 0x00110011;
  user->a12 = 0x00120012;
  user->a13 = 0x00130013;
  user->a14 = 0x00140014;
  user->a15 = 0x00160015;

  user->sp = usp_top;
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
  kprintf("r0:  %x\n", context->a0);
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