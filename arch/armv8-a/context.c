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


  
}

// #define DEBUG 1
void context_switch(interrupt_context_t* context, context_t** current,
                    context_t* next_context) {
  context_t* current_context = *current;

}

void context_dump(context_t* c) {
  
}

void context_dump_interrupt(interrupt_context_t* ic) {
  kprintf("pc:  %x\n", ic->pc);
  kprintf("cpsr:  %x\n", ic->psr);

  kprintf("sp:  %x\n", ic->sp);
  kprintf("lr:  %x\n", ic->lr);
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
#ifdef BACKTRACE
    cpu_backtrace(fp, buf, 8);
    kprintf("--backtrace--\n");
    for (int i = 0; i < 8; i++) {
      kprintf(" %8x\n", buf[i]);
    }
#endif
  }
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
  
}