/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "../cpu.h"

#include "context.h"
#include "cpu.h"

extern boot_info_t* boot_info;

u32 read_pc() {
  u32 val = 0;
  return val;
}

u32 read_fp() {
  u32 val = 0;
  return val;
}

u32 cpu_get_sp() {
  void* sp;
  asm volatile("mov %0, sp;" : "=r"(sp));
  return sp;
}

void cpu_set_vector(u32 addr) {
  kprintf("set vector a %x\n", addr);
  asm volatile("wsr.VECBASE %0" ::"r"(addr));
}

void cpu_set_page(u32 page_table) {}

void cpu_disable_page() {
  u32 reg;
  // read mmu
}

void cpu_enable_page() {
  u32 reg;

  dsb();
  isb();
}

inline void cpu_invalidate_tlbs(void) {}

void cpu_init() {}

void cpu_halt() {
  for (;;) {
  };
}

void cpu_wait(){
  
}

int cpu_get_id(){
  return 0;
}

u32 cpu_get_fault(){
  return 0;
}


ulong cpu_get_cs(void) {
  ulong result;

  return result;
}

int cpu_tas(volatile int* addr, int newval) {
  int result = newval;
  // asm volatile("lock; xchg %0, %1"
  //              : "+m"(*addr), "=r"(result)
  //              : "1"(newval)
  //              : "cc");
  return result;
}

// void context_restore(context_t* context){
//   cpu_sti();
//   interrupt_exit_context(context);
// }

void cpu_backtrace(void) {
  int topfp = read_fp();
  for (int i = 0; i < 10; i++) {
    u32 fp = *(((u32*)topfp) - 3);
    u32 sp = *(((u32*)topfp) - 2);
    u32 lr = *(((u32*)topfp) - 1);
    u32 pc = *(((u32*)topfp) - 0);
    if (i == 0) {
      kprintf("top frame %x\n", pc);
    }  // top frame
    if (fp != 0) {
      kprintf(" %x\n", lr);
    }  // middle frame
    else {
      kprintf("bottom frame %x\n", pc);
    }  // bottom frame, lr invalid
    if (fp == 0) break;
    topfp = fp;
  }
}

void* syscall0(u32 num) {
  int ret;

  return ret;
}

void* syscall1(u32 num, void* arg0) {
  int ret;

  return ret;
}
void* syscall2(u32 num, void* arg0, void* arg1) {
  int ret;

  return ret;
}
void* syscall3(u32 num, void* arg0, void* arg1, void* arg2) {
  u32 ret = 0;

  return ret;
}

void* syscall4(u32 num, void* arg0, void* arg1, void* arg2, void* arg3) {
  u32 ret = 0;

  return ret;
}

void* syscall5(u32 num, void* arg0, void* arg1, void* arg2, void* arg3,
               void* arg4) {
  u32 ret = 0;

  return ret;
}