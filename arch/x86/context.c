/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "../cpu.h"

extern boot_info_t* boot_info;

void context_init(context_t* context, u32* entry, u32* kstack, u32* ustack,
                  u32 level, int cpu) {
  if (context == NULL) {
    return;
  }
  tss_t* tss = &boot_info->tss[cpu];
  context->tss = tss;
  context->eip = entry;
  context->level = level;
  u32 cs, ds;
  if (level == 0) {
    cs = GDT_ENTRY_32BIT_CS * GDT_SIZE;
    ds = GDT_ENTRY_32BIT_DS * GDT_SIZE;
  } else if (level == 3) {
    cs = GDT_ENTRY_USER_32BIT_CS * GDT_SIZE | level;
    ds = GDT_ENTRY_USER_32BIT_DS * GDT_SIZE | level;
  } else {
    kprintf("not suppport level %d\n", level);
  }
  kstack = ((u32)kstack) - sizeof(interrupt_context_t);
  interrupt_context_t* ic = kstack;
  ic->ss = ds;          // ss
  ic->esp = ustack;     // usp
  ic->eflags = 0x0200;  // eflags
  ic->cs = cs;          // cs
  ic->eip = entry;      // eip 4

  ic->no = 0;             // no  5
  ic->code = 0;           // no  5
  ic->eax = 0;            // eax 6
  ic->ecx = 0;            // ecx 7
  ic->edx = 0;            // edx 8
  ic->ebx = 0;            // ebx 9
  ic->esp_null = ustack;  // esp 10
  ic->ebp = ustack;       // ebp 11
  ic->esi = 0;            // esi 12
  ic->edi = 0;            // edi 13
  ic->ds = ds;            // ds  14
  ic->es = ds;            // es  15
  ic->fs = ds;            // fs  16
  ic->gs = ds;            // gs    17

  context->ksp = ic;
  context->ss0 = GDT_ENTRY_32BIT_DS * GDT_SIZE;
  context->ds0 = GDT_ENTRY_32BIT_DS * GDT_SIZE;
  context->usp = ustack;
  context->ss = ds;
  context->ds = ds;

  ulong addr = (ulong)boot_info->pdt_base;
  // must not overwrite page_dir
  if (context->upage == NULL) {
    context->upage = addr;
  }
  context->kpage = addr;

  if (tss->eip == 0 && tss->cr3 == 0) {
    tss->ss0 = context->ss0;
    tss->esp0 = context->ksp + sizeof(interrupt_context_t);
    tss->eip = context->eip;
    tss->ss = ds;
    tss->ds = tss->es = tss->fs = tss->gs = ds;
    tss->cs = cs;
    tss->esp = context->usp;
    tss->cr3 = boot_info->pdt_base;
    set_ldt(GDT_ENTRY_32BIT_TSS * GDT_SIZE);
  }
}

int context_get_mode(context_t* context) {
  int mode = 0;
  if (context != NULL) {
    return context->level;
  }
  return mode;
}

void context_dump(context_t* c) {
  kprintf("eip:     %x\n", c->eip);
  kprintf("ksp:    %x\n", c->ksp);
  kprintf("usp:     %x\n", c->usp);

  kprintf("user page: %x\n", c->upage);
  kprintf("kernel page: %x\n", c->kpage);

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
  kprintf("esp: %8x ebp: %x\n", ic->esp, ic->ebp);
  kprintf("--segment registers--\n");
  kprintf("cs: %2x ss: %x\n", cs, ss);
  kprintf("ds: %2x es: %x\n", ds, es);
  kprintf("fs: %2x gs: %x\n", fs, gs);
  kprintf("cr2: %8x cr3: %8x\n", cr2, cr3);

  if (ic->ebp > 1000) {
    int buf[10];
    stack_frame_t* fp = ic->ebp;
    cpu_backtrace(fp, buf, 4);
    kprintf("--backtrace--\n");
    for (int i = 0; i < 4; i++) {
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

void context_clone(context_t* des, context_t* src) {
  //这里重点关注 usp ksp page_dir 3个变量的复制

  interrupt_context_t* ic = des->ksp;
  interrupt_context_t* is = src->ksp;

  // not cover page_dir
  void* page = des->upage;
  *des = *src;
  des->upage = page;

  // ic = ((u32)ic) - sizeof(interrupt_context_t);

  if (ic != NULL) {
    *ic = *is;  // set usp alias ustack and ip cs ss and so on
    ic->eflags = 0x0200;
  }
  des->ksp = (u32)ic;  // set ksp alias ustack
  des->usp = src->usp;
}

void context_switch(interrupt_context_t* ic, context_t** current,
                    context_t* next_context) {
  context_t* current_context = *current;

  if (ic == NULL) {
    ic = current_context->ksp;
    ic->esp = current_context->usp;
  } else {
    current_context->ksp = ic;
    current_context->usp = ic->esp;
  }

  interrupt_context_t* c = next_context->ksp;

  tss_t* tss = next_context->tss;
  tss->esp = next_context->ksp + sizeof(interrupt_context_t);
  tss->ss0 = next_context->ss0;
  tss->cr3 = next_context->upage;
  *current = next_context;
  if (next_context->upage != NULL) {
    context_switch_page(next_context->upage);
  }
}
