/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "../cpu.h"

extern boot_info_t* boot_info;

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

  tss_t* tss = &boot_info->tss[cpu];
  context->tss = tss;
  context->eip = entry;
  context->level = level;
  context->cpu = cpu;
  u32 cs, ds;
  if (level == 0) {
    cs = GDT_ENTRY_32BIT_CS * GDT_SIZE;
    ds = GDT_ENTRY_32BIT_DS * GDT_SIZE;
  } else if (level == 3) {
    cs = GDT_ENTRY_USER_32BIT_CS * GDT_SIZE | level;
    ds = GDT_ENTRY_USER_32BIT_DS * GDT_SIZE | level;
  } else {
    log_error("not suppport level %d\n", level);
  }

  interrupt_context_t* ic = (u32)ksp_top - sizeof(interrupt_context_t);
  ic->ss = ds;          // ss
  ic->esp = usp_top;    // usp
  ic->eflags = 0x0200;  // eflags
  ic->cs = cs;          // cs
  ic->eip = entry;      // eip 4

  ic->no = 0;              // no  5
  ic->code = 0;            // no  5
  ic->eax = 0;             // eax 6
  ic->ecx = 0;             // ecx 7
  ic->edx = 0;             // edx 8
  ic->ebx = 0;             // ebx 9
  ic->esp_null = usp_top;  // esp 10
  ic->ebp = usp_top;       // ebp 11
  ic->esi = 0;             // esi 12
  ic->edi = 0;             // edi 13
  ic->ds = ds;             // ds  14
  ic->es = ds;             // es  15
  ic->fs = ds;             // fs  16
  ic->gs = ds;             // gs    17

  context->ksp = ic;
  context->ss0 = GDT_ENTRY_32BIT_DS * GDT_SIZE;
  context->ds0 = GDT_ENTRY_32BIT_DS * GDT_SIZE;
  context->usp = usp_top;
  context->ss = ds;
  context->ds = ds;

  if (tss->eip == 0 && tss->cr3 == 0) {
    tss->ss0 = context->ss0;
    tss->esp0 = ((u32)context->ksp) + sizeof(interrupt_context_t);
    tss->eip = context->eip;
    tss->ss = ds;
    tss->ds = tss->es = tss->fs = tss->gs = ds;
    tss->cs = cs;
    tss->esp = context->usp;
    tss->cr3 = boot_info->pdt_base;
    set_ldt(GDT_ENTRY_32BIT_TSS * GDT_SIZE);
  }
  return 0;
}

int context_get_mode(context_t* context) {
  int mode = 0;
  if (context != NULL) {
    return context->level;
  }
  return mode;
}

void context_dump(context_t* c) {
  kprintf("eip: %8x\n", c->eip);
  kprintf("ksp: %8x\n", c->ksp);
  kprintf("usp: %8x\n", c->usp);

  if (c->ksp != 0) {
    context_dump_interrupt(c->ksp);
  }
}

void context_dump_interrupt(interrupt_context_t* ic) {
  u32 cr2, cr3;
  u32 ds, es, fs, gs, cs, ss;
  asm volatile("movl	%%cr2,	%%eax" : "=a"(cr2));
  asm volatile("movl %%cr3,	%%eax" : "=a"(cr3));
  asm volatile("movl %%ds,	%%eax" : "=a"(ds));
  asm volatile("movl %%es,	%%eax" : "=a"(es));
  asm volatile("movl %%fs,	%%eax" : "=a"(fs));
  asm volatile("movl %%gs,	%%eax" : "=a"(gs));
  asm volatile("movl %%cs,	%%eax" : "=a"(cs));
  asm volatile("movl %%ss,	%%eax" : "=a"(ss));

  kprintf("--interrupt context segment registers--\n");
  kprintf("code: %2x eflags: %8x\n", ic->no, ic->eflags);
  kprintf("cs: %4x eip: %8x \n", ic->cs, ic->eip);
  kprintf("ss: %4x esp: %8x\n", ic->ss, ic->esp);
  // kprintf("old ss:\t%x\told usp:%x\n", old_ss, old_usp);
  kprintf("--interrupt context genernal registers--\n");
  kprintf("eax: %8x ebx: %8x\n", ic->eax, ic->ebx);
  kprintf("ecx: %8x edx: %8x\n", ic->ecx, ic->edx);
  kprintf("esi: %8x edi: %8x\n", ic->esi, ic->edi);
  kprintf("esp: %8x ebp: %8x\n", ic->esp, ic->ebp);
  kprintf("--segment registers--\n");
  kprintf("cs: %2x ss: %x\n", cs, ss);
  kprintf("ds: %2x es: %x\n", ds, es);
  kprintf("fs: %2x gs: %x\n", fs, gs);
  kprintf("cr2: %8x cr3: %8x\n", cr2, cr3);

  if (ic->ebp > 1000) {
    int buf[10];
    stack_frame_t* fp = ic->ebp;
    cpu_backtrace(fp, buf, 10);
    kprintf("--backtrace--\n");
    for (int i = 0; i < 10; i++) {
      kprintf(" %8x\n", buf[i]);
    }
  }
}

void context_dump_fault(interrupt_context_t* ic, u32 fault_addr) {
  int present = ic->code & 0x1;  // present
  int rw = ic->code & 0x2;       // rw
  int us = ic->code & 0x4;       // user mode
  int reserved = ic->code & 0x8;
  int id = ic->code & 0x10;
  kprintf("--dump page fault--\n");
  context_dump_interrupt(ic);
  kprintf("page: [");
  if (present == 1) {
    kprintf("present ");
  }
  if (rw) {
    kprintf("read-only ");
  }
  if (us) {
    kprintf("user-mode ");
  }
  if (reserved) {
    kprintf("reserved ");
  }
  kprintf("]\n");
  kprintf("fault: 0x%x \n", fault_addr);
  kprintf("----------------------------\n");
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

  // context_t* pdes = page_v2p(des->upage, des);

  // 这里重点关注 usp ksp upage 3个变量的复制
  u32* ksp_end = (u32)des->ksp_end - sizeof(interrupt_context_t);

  interrupt_context_t* ic = ksp_end;
  interrupt_context_t* is = src->ksp;

  if (ic != NULL) {
    *ic = *is;  // set usp alias ustack and ip cs ss and so on
    ic->eflags = 0x0200;
  }
  // set ksp alias ustack
  des->ksp = (u32)ic;
  // pdes->ksp = des->ksp;

  return 0;
}

interrupt_context_t* context_switch(interrupt_context_t* ic, context_t* current,
                    context_t* next) {
  if(ic==NULL){
    return;
  }
  // kmemcpy(current->ksp, ic, sizeof(interrupt_context_t));
  // kmemcpy(ic, next->ksp, sizeof(interrupt_context_t));

  context_save(ic, current);
  tss_t* tss = next->tss;
  tss->esp0 = (u32)next->ksp + sizeof(interrupt_context_t);
  tss->ss0 = next->ss0;
  tss->esp = next->usp;

  return next->ksp;
}

void context_switch_page(context_t* context, u32 page_dir) {
  tss_t* tss = context->tss;
  tss->cr3 = page_dir;
  cpu_set_page(page_dir);
}

void context_save(interrupt_context_t* ic, context_t* current) {
  if (ic == NULL) {
    return;
  }
  current->ksp = ic;
  current->usp = ic->esp;
}
