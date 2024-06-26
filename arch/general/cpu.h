/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef ARM_CPU_H
#define ARM_CPU_H

#include "arch/boot.h"
#include "libs/include/types.h"

#define debugger


typedef struct cpsr {
  union {
    struct {
      u32 M : 5;
      u32 T : 1;
      u32 F : 1;
      u32 I : 1;
      u32 A : 1;
      u32 E : 1;
      u32 RESERVED2 : 6;
      u32 GE : 4;
      u32 RESERVED1 : 4;
      u32 J : 1;
      u32 Res : 2;
      u32 Q : 1;
      u32 V : 1;
      u32 C : 1;
      u32 Z : 1;
      u32 N : 1;
    };
    u32 val;
  };
} __attribute__((packed)) cpsr_t;

typedef u32 (*sys_call_fn)(u32 arg1, u32 arg2, u32 arg3, u32 arg4, u32 arg5);

#define sys_fn_call(duck_interrupt_context, fn)               \
  duck_interrupt_context->eax = ((sys_call_fn)fn)(             \
      duck_interrupt_context->edi, duck_interrupt_context->esi, \
      duck_interrupt_context->edx, duck_interrupt_context->ecx, \
      duck_interrupt_context->ebx)


#define cpu_cli() 
#define cpu_sti() 
#define cpu_cpl() (cpu_get_cs() & 0x3)
u32 cpu_get_id();

#define cpu_faa(ptr) 1//__sync_fetch_and_add(ptr, 1)


#endif