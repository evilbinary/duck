/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "../cpu.h"

#include "context.h"
#include "cpu.h"

extern boot_info_t* boot_info;
u32 cpus_id[MAX_CPU];

int cpu_get_number() { return boot_info->tss_number; }

u32 cpu_get_id() {
  int cpu = 0;
#if MP_ENABLE
  // __asm__ volatile("mrc p15, #0, %0, c0, c0, #5\n" : "=r"(cpu));
#endif
  return cpu & 0xf;
}

u32 cpu_get_index(int idx) {
  if (idx < 0 || idx > cpu_get_number()) {
    kprintf("out of bound get cpu idx\n");
    return 0;
  }
  return cpus_id[idx];
}

// cpu 初始化
int cpu_init_id(u32 id) { return 0; }

// 启动cpu
int cpu_start_id(u32 id, u32 entry) { return 0; }

// cpu 延迟
void cpu_delay(int n) { for (int i = 0; i < 10000 * n; i++); }

int cpu_pmu_version() {
  u32 pmu_id = 0;

  return (pmu_id >> 4) & 0xF;
}

void cpu_pmu_enable(int enable, u32 timer) {
  if (enable == 1) {
  } else if (enable == 0) {
  }
}

unsigned int cpu_cyclecount(void) {
  unsigned int value = 0;

  return value;
}

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
  asm volatile("wsr %0,vecbase" ::"r"(addr));
  asm volatile("rsync\n");
}

cpsr_t cpu_read_ps() {
  cpsr_t d ;
  d.val = RSR(PS);

  return d;
}

void cpu_write_ps(cpsr_t ps) { WSR(PS, ps.val); }

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

void cpu_init() {
  interrupt_init();
  interrupt_regist_all();
}

void cpu_halt() {
  for (;;) {
    cpu_wait();
  }
}

void cpu_wait() { __asm__ volatile("waiti 0" : : : "memory"); }

u32 cpu_get_fault() { return 0; }

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