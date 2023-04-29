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

  interrupt_context_t* ic = (u32)ksp_top - sizeof(interrupt_context_t) * 2;
  kmemset(ic, 0, sizeof(interrupt_context_t));

  if (level == 0) {
    ic->sstatus = 1 << 8;                // SPP
    ic->sstatus = ic->sstatus | 1 << 1;  // SIE
    ic->sstatus = ic->sstatus | 1 << 5;  // SPIE

    ic->sstatus = ic->sstatus | 1 << 18;  // SUM
    ic->sstatus = ic->sstatus | 1 << 19;  // MXR
    

  } else if (level == 3) {
    ic->sstatus = 0 << 8;                // SPP
    ic->sstatus = ic->sstatus | 1 << 1;  // SIE
    ic->sstatus = ic->sstatus | 1 << 5;  // SPIE

  } else {
    kprintf("not suppport level %d\n", level);
  }

  ic->ra = entry;
  ic->sepc = entry;
  ic->sp = usp_top;
  context->usp = usp_top;
  context->ksp = ic;
}

void context_dump(context_t* c) {
  kprintf("tid: %8x\n", c->tid);
  kprintf("eip: %8x\n", c->eip);
  kprintf("ksp: %8x\n", c->ksp);
  kprintf("usp: %8x\n", c->usp);

  kprintf("--interrupt context--\n");
  interrupt_context_t* ic = c->ksp;
  if (ic != NULL) {
    context_dump_interrupt(ic);
  }
}

void context_dump_interrupt(interrupt_context_t* ic) {
  kprintf("ra:  %x\n", ic->ra);
  kprintf("sstatus:  %x\n", ic->sstatus);
  kprintf("sepc:  %x\n", ic->sepc);

  kprintf("sp:  %x\n", ic->sp);
  kprintf("gp:  %x\n", ic->gp);
  kprintf("tp:  %x\n", ic->tp);
  kprintf("t0:  %x\n", ic->t0);
  kprintf("t1:  %x\n", ic->t1);
  kprintf("t2:  %x\n", ic->t2);

  kprintf("a0:  %x\n", ic->a0);
  kprintf("a1:  %x\n", ic->a1);
  kprintf("a2:  %x\n", ic->a2);
  kprintf("a3:  %x\n", ic->a3);
  kprintf("a4:  %x\n", ic->a4);
  kprintf("a5:  %x\n", ic->a5);
  kprintf("a6:  %x\n", ic->a6);
  kprintf("a7:  %x\n", ic->a7);

  kprintf("s0:  %x\n", ic->s0);
  kprintf("s1:  %x\n", ic->s1);
  kprintf("s2:  %x\n", ic->s2);
  kprintf("s3:  %x\n", ic->s3);
  kprintf("s4:  %x\n", ic->s4);
  kprintf("s5:  %x\n", ic->s5);
  kprintf("s6:  %x\n", ic->s6);
  kprintf("s7:  %x\n", ic->s7);
  kprintf("s8:  %x\n", ic->s8);
  kprintf("s9:  %x\n", ic->s9);
  kprintf("s10:  %x\n", ic->s10);
  kprintf("s11:  %x\n", ic->s11);

  if (ic->s0 > 1000) {
    int buf[10];
    void* fp = ic->s0;
#ifdef BACKTRACE
    cpu_backtrace(fp, buf, 8);
    kprintf("--backtrace--\n");
    for (int i = 0; i < 8; i++) {
      kprintf(" %8x\n", buf[i]);
    }
#endif
  }
}

void context_dump_fault(interrupt_context_t* context, u32 fault_addr) {}

int context_clone(context_t* des, context_t* src) {
  if (src->ksp_start == NULL) {
    log_error("ksp top is null\n");
    return -1;
  }
  if (des->ksp_start == NULL) {
    log_error("ksp top is null\n");
    return -1;
  }
  kmemmove(des->ksp_start, src->ksp_start, src->ksp_size);
  // todo
}

// #define DEBUG 1
interrupt_context_t* context_switch(interrupt_context_t* ic, context_t* current,
                                    context_t* next) {
  if (ic == NULL) {
    return;
  }
  current->ic =ic;
  current->ksp = ic;
  current->usp = ic->sp;

  return next->ksp;
}
