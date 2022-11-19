/*******************************************************************
 * Copyright 2021-2080 evilbinary
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

int context_init(context_t* context, u32* entry, u32 level, int cpu) {
  if (context == NULL) {
    return -1;
  }
  if (context->ksp_start == NULL || context->usp_start == NULL) {
    log_error("ksp start or usp start is null\n");
    return -1;
  }
  if (context->ksp_end == NULL || context->ksp_end == NULL) {
    log_error("ksp end or usp end is null\n");
    return -1;
  }
  // u32 ksp_top = (u32)context->ksp_end;
  u32 ksp_top = ((u32)context->ksp_end) - sizeof(interrupt_context_t);

  u32 usp_top = context->usp;

  context->eip = entry;
  context->level = level;
  context->ksp = ksp_top;
  u32 cs, ds;
  cpsr_t cpsr;
  cpsr.val = 0;
  if (level == 0) {
    // kernel mode
    cpsr.Z = 1;
    cpsr.C = 1;
    cpsr.A = 1;
    cpsr.I = 0;
    cpsr.F = 1;
    cpsr.M = 0x1f;
  } else if (level == 3) {
    cpsr.I = 0;
    cpsr.F = 0;
    cpsr.T = 0;  // arm
    cpsr.M = 0x10;
  } else {
    kprintf("not suppport level %d\n", level);
  }

  interrupt_context_t* ic = ksp_top;
  kmemset(ic, 0, sizeof(interrupt_context_t));
  ic->lr = entry;  // r14
  ic->lr += 4;
  ic->psr = cpsr.val;
  ic->r0 = 0;
  ic->r1 = 0x00010001;
  ic->r2 = 0x00020002;
  ic->r3 = 0x00030003;
  ic->r4 = 0x00040004;
  ic->r5 = 0x00050006;
  ic->r6 = 0x00060006;
  ic->r7 = 0x00070007;
  ic->r8 = 0x00080008;
  ic->r9 = 0x00090009;
  ic->r10 = 0x00100010;
  ic->r11 = 0x00110011;  // fp
  ic->r12 = 0x00120012;  // ip
  ic->sp = usp_top;      // r13
  ic->lr0 = ic->lr;
  context->usp = usp_top;
  context->ksp = ic;

  ulong addr = (ulong)boot_info->pdt_base;
  context->kpage = addr;
  // must not overwrite upage
  if (context->upage == NULL) {
    context->upage = addr;
  }
#ifdef PAGE_CLONE
  context->upage = page_alloc_clone(addr);
#endif
}

void context_dump(context_t* c) {
  kprintf("eip: %8x\n", c->eip);
  kprintf("ksp: %8x\n", c->ksp);
  kprintf("usp: %8x\n", c->usp);

  kprintf("upage: %x\n", c->upage);
  kprintf("kernel upage: %x\n", c->kpage);
  kprintf("--interrupt context--\n");
  interrupt_context_t* ic = c->ksp;
  context_dump_interrupt(ic);
}

void context_dump_interrupt(interrupt_context_t* ic) {
  kprintf("lr:  %x cpsr:%x\n", ic->lr, ic->psr);
  kprintf("sp:  %x\n", ic->sp);
  kprintf("r0:  %x\n", ic->r0);
  kprintf("r1:  %x\n", ic->r1);
  kprintf("r2:  %x\n", ic->r2);
  kprintf("r3:  %x\n", ic->r3);
  kprintf("r4:  %x\n", ic->r4);
  kprintf("r5:  %x\n", ic->r5);
  kprintf("r6:  %x\n", ic->r6);
  kprintf("r7:  %x\n", ic->r7);
  kprintf("r8:  %x\n", ic->r8);
  kprintf("r9:  %x\n", ic->r9);
  kprintf("r10: %x\n", ic->r10);
  kprintf("r11(fp): %x\n", ic->r11);
  kprintf("r12(ip): %x\n", ic->r12);
  if (ic->r11 > 1000) {
    int buf[10];
    void* fp = ic->r11;
    cpu_backtrace(fp, buf, 3);
    kprintf("--backtrace--\n");
    for (int i = 0; i < 3; i++) {
      kprintf(" %8x\n", buf[i]);
    }
  }
}

void context_dump_fault(interrupt_context_t* context, u32 fault_addr) {
  kprintf("----------------------------\n");
  kprintf("ifsr: %x dfsr: %x dfar: %x\n", read_ifsr(), read_dfsr(),
          read_dfar());
  kprintf("pc: %x\n", read_pc());
  context_dump_interrupt(context);
  kprintf("fault: 0x%x \n", fault_addr);
  kprintf("----------------------------\n\n");
}

int context_clone(context_t* des, context_t* src) {
  if (src->ksp_start == NULL || src->usp_start == NULL) {
    log_error("ksp top or usp top is null\n");
    return -1;
  }
  if (des->ksp_start == NULL || des->usp_start == NULL) {
    log_error("ksp top or usp top is null\n");
    return -1;
  }
  context_t* pdes = virtual_to_physic(des->upage, des);

  //这里重点关注 usp ksp upage 3个变量的复制
  u32* ksp_end = (u32)des->ksp_end - sizeof(interrupt_context_t);
  u32* usp_end = des->usp_end;

  interrupt_context_t* ic = ksp_end;
  interrupt_context_t* is = src->ksp;
#if DEBUG
  kprintf("------context clone dump src--------------\n");
  context_dump(src);
#endif
  // not cover upage
  void* page = des->upage;
  des->upage = page;
  pdes->upage = page;

  if (ic != NULL) {
    // set usp alias ustack and ip cs ss and so on
    kmemmove(ic, is, sizeof(interrupt_context_t));
    cpsr_t cpsr;
    cpsr.val = 0;
    cpsr.Z = 1;
    cpsr.C = 1;
    cpsr.A = 1;
    cpsr.I = 1;
    cpsr.F = 1;
    cpsr.T = 0;

    cpsr.M = 0x13;
    ic->psr = cpsr.val;
  }
  des->ksp = (u32)ic;  // set ksp alias ustack
  pdes->ksp = des->ksp;
#if DEBUG
  kprintf("------context clone dump des--------------\n");
  context_dump(des);
#endif
}

// #define DEBUG 1
void context_switch(interrupt_context_t* ic, context_t** current,
                    context_t* next_context) {
  context_t* current_context = *current;
#if DEBUG
  kprintf("-----switch dump current------\n");
  context_dump(current_context);
#endif
  if (ic == NULL) {
    ic = current_context->ksp;
    ic->sp = current_context->usp;
  } else {
    current_context->ksp = ic;
    current_context->usp = ic->sp;
  }
  *current = next_context;
  context_switch_page(next_context->upage);
#if DEBUG
  kprintf("-----switch dump next------\n");
  context_dump(next_context);
  kprintf("\n");
#endif
}