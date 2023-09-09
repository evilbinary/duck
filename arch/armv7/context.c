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
    if ((c->psr & 0x1F) == 0x10) {
      return 3;  // user mode
    }
  }
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
  u32 cs, ds;
  cpsr_t cpsr;
  cpsr.val = 0x1000000;
  if (level == 0) {
    // kernel mode
    // cpsr.Z = 0;
    // cpsr.C = 0;
  } else if (level == 3) {
    // cpsr.Z = 0;
    // cpsr.C = 0;
  } else {
    kprintf("not suppport level %d\n", level);
  }

  interrupt_context_t* user = usp_top;
  kmemset(user, 0, sizeof(interrupt_context_t));
  user->lr = 0xFFFFFFFD;
  user->pc = entry;
  user->psr = cpsr.val;
  user->r0 = 0;
  user->r1 = 0x00010001;
  user->r2 = 0x00020002;
  user->r3 = 0x00030003;
  user->r4 = 0x00040004;
  user->r5 = 0x00050005;
  user->r6 = 0x00060006;
  user->r7 = 0x00070007;
  user->r8 = 0x00080008;
  user->r9 = 0x00090009;
  user->r10 = 0x00100010;
  user->r11 = 0x00110011;  // fp
  user->r12 = 0x00120012;  // ip

  context->usp = usp_top;
  context->ksp = ksp_top;

  return 0;
}

#define DEBUG 0
interrupt_context_t* context_switch(interrupt_context_t* ic, context_t* current,
                    context_t* next_context) {
#if DEBUG
  kprintf("-----switch dump current------\n");
  context_dump(current);
#endif
  current->usp = (u32)ic;
  current->eip = ic->pc;
  
 

#if DEBUG
  kprintf("-----switch dump next------\n");
  context_dump(next_context);
  kprintf("\n");
  kprintf("\n");
#endif

 return next_context->usp;
}

void context_dump(context_t* c) {
  kprintf("tid: %8x\n", c->tid);
  kprintf("eip: %8x\n", c->eip);
  kprintf("ksp: %8x\n", c->ksp);
  kprintf("usp: %8x\n", c->usp);

  kprintf("--interrupt context--\n");
  interrupt_context_t* context = c->usp;
  context_dump_interrupt(context);
}

void context_dump_interrupt(interrupt_context_t* context) {
  kprintf("lr:  %x cpsr:%x\n", context->lr, context->psr);
  kprintf("sp:  %x\n", context);
  kprintf("r0:  %x\n", context->r0);
  kprintf("r1:  %x\n", context->r1);
  kprintf("r2:  %x\n", context->r2);
  kprintf("r3:  %x\n", context->r3);
  kprintf("r4:  %x\n", context->r4);
  kprintf("r5:  %x\n", context->r5);
  kprintf("r6:  %x\n", context->r6);
  kprintf("r7:  %x\n", context->r7);
  kprintf("r8:  %x\n", context->r8);
  kprintf("r9:  %x\n", context->r9);
  kprintf("r10: %x\n", context->r10);
  kprintf("r11(fp): %x\n", context->r11);
  kprintf("r12(ip): %x\n", context->r12);
}

void context_dump_fault(interrupt_context_t* context, u32 fault_addr) {
  kprintf("----------------------------\n");
  kprintf("ifsr: %x dfsr: %x dfar: %x\n", read_ifsr(), read_dfsr(),
          read_dfar());
  kprintf("current pc: %x\n", read_pc());
  context_dump_interrupt(context);
  kprintf("fault: 0x%x \n", fault_addr);
  kprintf("----------------------------\n\n");
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

  des->eip = src->eip;
  des->level = src->level;

  // 这里重点关注 usp ksp
  kmemmove(des->ksp_start, src->ksp_start, src->ksp_size);

  u32 offset = src->ksp_end - (u32)src->ksp;
  interrupt_context_t* ic = des->ksp_end - offset;
  interrupt_context_t* is = src->ksp;

#if DEBUG
  kprintf("------context clone dump src--------------\n");
  context_dump(src);
#endif

  des->ksp = ic;
  cpsr_t cpsr;
  cpsr.val = 0;
  cpsr.Z = 0;
  cpsr.C = 0;
  cpsr.T = 0;
  ic->psr = cpsr.val;
  des->usp = src->usp;
#if DEBUG
  kprintf("------context clone dump des--------------\n");
#endif
  context_dump(des);

  return 0;
}