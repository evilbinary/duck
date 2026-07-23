/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef X86_CPU_H
#define X86_CPU_H

#include "arch/boot.h"
#include "libs/include/types.h"

#define debugger asm("xchg %bx,%bx\n")

typedef u32 (*sys_call_fn)(u32 arg1, u32 arg2, u32 arg3, u32 arg4, u32 arg5
                           );

#define sys_fn_call(duck_interrupt_context, fn)               \
  duck_interrupt_context->eax = ((sys_call_fn)fn)(             \
      duck_interrupt_context->edi, duck_interrupt_context->esi, \
      duck_interrupt_context->edx, duck_interrupt_context->ecx, \
      duck_interrupt_context->ebx)


#define cpu_cli() asm("cli")
#define cpu_sti() asm("sti")
#define cpu_cpl() (cpu_get_cs() & 0x3)
#define cpu_faa(ptr) 1//__sync_fetch_and_add(ptr, 1)


int cpu_get_number();
int cpu_start_id(u32 id, u32 entry);
int cpu_init_id(u32 id);
u32 cpu_get_id();

#endif