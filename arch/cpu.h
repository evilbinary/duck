/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef CPU_H
#define CPU_H

#include "boot.h"
#include "libs/include/types.h"
#include "interrupt.h"

#ifdef ARM
  #ifdef ARMV7_A
    #include "armv7-a/cpu.h"
  #elif defined(ARMV7)
    #include "armv7/cpu.h"
  #elif defined(ARMV5)
    #include "armv5/cpu.h"
  #elif defined(ARMV8_A)
    #include "armv8-a/cpu.h"
  #else
    #error "no support arm"
  #endif
#elif defined(ARM64)
  #if defined(ARMV8_A)
    #include "armv8-a/cpu.h"
  #else
    #error "ARM64 defined but no specific architecture (e.g., ARMV8_A)"
  #endif
#elif defined(X86)
  #ifdef X86_DUCK
    #include "x86-duck/cpu.h"
  #else
    #include "x86/cpu.h"
  #endif
#elif defined(LX6)
  #include "lx6/cpu.h"
#elif defined(GENERAL)
#include "general/cpu.h"
#elif defined(RISCV)
#include "riscv/cpu.h"
#elif defined(DUMMY)
#include "dummy/cpu.h"
#else
    #error "no support"
#endif


typedef uint32_t phys_address_t;
typedef uint32_t virtual_address_t;

typedef struct stack_frame {
  struct stack_frame* prev;
  void* return_addr;
} __attribute__((packed)) stack_frame_t;

void cpu_init(int cpu);
void cpu_halt();
void cpu_wait(void);

#define KERNEL_MODE 0
#define USER_MODE 3
#define GET_CPL(x) (((x)&0x03))  //0-3

int cpu_tas(volatile int* addr, int newval);

/* 通用 SP 读写：perf/backtrace 切换大栈用，覆盖所有架构 */
static inline u32 cpu_get_sp(void) {
#if defined(X86)
  u32 sp;
  __asm__ volatile("mov %%esp, %0" : "=r"(sp));
  return sp;
#elif defined(RISCV)
  u32 sp;
  __asm__ volatile("mv %0, sp" : "=r"(sp));
  return sp;
#elif defined(LX6)
  u32 sp;
  __asm__ volatile("mov %0, sp;" : "=r"(sp));
  return sp;
#elif defined(ARM) || defined(ARM64)
  u32 sp;
  __asm__ volatile("mov %0, sp" : "=r"(sp));
  return sp;
#else /* GENERAL / DUMMY */
  return 0;
#endif
}

static inline void cpu_set_sp(u32 sp) {
#if defined(X86)
  __asm__ volatile("mov %0, %%esp" ::"r"(sp));
#elif defined(RISCV)
  __asm__ volatile("mv sp, %0" ::"r"(sp));
#elif defined(LX6)
  __asm__ volatile("mov sp, %0;" ::"r"(sp));
#elif defined(ARM) || defined(ARM64)
  __asm__ volatile("mov sp, %0" ::"r"(sp));
#else
  (void)sp;
#endif
}

#endif