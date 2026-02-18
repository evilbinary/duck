/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef CONTEXT_H
#define CONTEXT_H

#include "arch/boot.h"
#include "libs/include/types.h"
#include "platform/platform.h"

// ARM64 (AArch64) interrupt context
// Layout matches save_context/restore_context in exception.S
// Following armv7-a pattern: no,code at sp, then psr,pc, then registers
typedef struct interrupt_context {
  // At sp+0, sp+8 (pushed last)
  u64 no;
  u64 code;
  
  // At sp+16, sp+24
  u64 psr;        // PSTATE (stored as u64)
  u64 pc;         // Exception return address (from ELR_EL1)
  
  // At sp+32 onwards (pushed first, at lowest addresses)
  u64 x0;
  u64 x1;
  u64 x2;
  u64 x3;
  u64 x4;
  u64 x5;
  u64 x6;
  u64 x7;
  u64 x8;
  u64 x9;
  u64 x10;
  u64 x11;
  u64 x12;
  u64 x13;
  u64 x14;
  u64 x15;
  u64 x16;
  u64 x17;
  u64 x18;
  u64 x19;
  u64 x20;
  u64 x21;
  u64 x22;
  u64 x23;
  u64 x24;
  u64 x25;
  u64 x26;
  u64 x27;
  u64 x28;
  u64 x29;
  u64 lr;         // Link register (X30)
  u64 sp;         // User stack pointer (SP_EL0)
} __attribute__((packed)) interrupt_context_t;

typedef struct context_t {
  interrupt_context_t* ic;
  interrupt_context_t* ksp;
  u64 usp;
  u64 eip;
  u32 level;
  u32 tid;

  u64 ksp_start;
  u64 ksp_end;

  u64 usp_size;
  u64 ksp_size;
} context_t;

// ARM64 exception macros
#define interrupt_process(X) \
  asm volatile(              \
      "stp x29, x30, [sp, #-16]!\n" \
      "blx " #X              \
      "\n"                   \
      "ldp x29, x30, [sp], #16\n" \
      :                      \
      :)

// Save context on exception entry
#define interrupt_entering_code(VEC, CODE) \
  asm volatile(                                  \
      "stp x29, x30, [sp, #-16]!\n" \
      "stp x0, x1, [sp, #-16]!\n" \
      "stp x2, x3, [sp, #-16]!\n" \
      "stp x4, x5, [sp, #-16]!\n" \
      "stp x6, x7, [sp, #-16]!\n" \
      "stp x8, x9, [sp, #-16]!\n" \
      "stp x10, x11, [sp, #-16]!\n" \
      "stp x12, x13, [sp, #-16]!\n" \
      "stp x14, x15, [sp, #-16]!\n" \
      "stp x16, x17, [sp, #-16]!\n" \
      "stp x18, x19, [sp, #-16]!\n" \
      "stp x20, x21, [sp, #-16]!\n" \
      "stp x22, x23, [sp, #-16]!\n" \
      "stp x24, x25, [sp, #-16]!\n" \
      "stp x26, x27, [sp, #-16]!\n" \
      "stp x28, x29, [sp, #-16]!\n" \
      "mov x0, %0\n" \
      "mov x1, %1\n" \
      "stp x0, x1, [sp, #-16]!\n" \
      :                                          \
      : "i"(VEC), "i"(CODE))

#define interrupt_exit_context(ksp) \
  asm volatile(                              \
      "mov sp, %0 \n"                         \
      "ldp x0, x1, [sp], #16\n" \
      "ldp x28, x29, [sp], #16\n" \
      "ldp x26, x27, [sp], #16\n" \
      "ldp x24, x25, [sp], #16\n" \
      "ldp x22, x23, [sp], #16\n" \
      "ldp x20, x21, [sp], #16\n" \
      "ldp x18, x19, [sp], #16\n" \
      "ldp x16, x17, [sp], #16\n" \
      "ldp x14, x15, [sp], #16\n" \
      "ldp x12, x13, [sp], #16\n" \
      "ldp x10, x11, [sp], #16\n" \
      "ldp x8, x9, [sp], #16\n" \
      "ldp x6, x7, [sp], #16\n" \
      "ldp x4, x5, [sp], #16\n" \
      "ldp x2, x3, [sp], #16\n" \
      "ldp x0, x1, [sp], #16\n" \
      "ldp x29, x30, [sp], #16\n" \
      "eret\n" \
      :                                      \
      : "r"(ksp))

#define interrupt_exit()           \
  asm volatile(                    \
      "ldp x0, x1, [sp], #16\n" \
      "ldp x28, x29, [sp], #16\n" \
      "ldp x26, x27, [sp], #16\n" \
      "ldp x24, x25, [sp], #16\n" \
      "ldp x22, x23, [sp], #16\n" \
      "ldp x20, x21, [sp], #16\n" \
      "ldp x18, x19, [sp], #16\n" \
      "ldp x16, x17, [sp], #16\n" \
      "ldp x14, x15, [sp], #16\n" \
      "ldp x12, x13, [sp], #16\n" \
      "ldp x10, x11, [sp], #16\n" \
      "ldp x8, x9, [sp], #16\n" \
      "ldp x6, x7, [sp], #16\n" \
      "ldp x4, x5, [sp], #16\n" \
      "ldp x2, x3, [sp], #16\n" \
      "ldp x0, x1, [sp], #16\n" \
      "ldp x29, x30, [sp], #16\n" \
      "eret\n" \
      :                            \
      :)

#define interrupt_entering(VEC) interrupt_entering_code(VEC, 0)

#define context_switch_page(ctx, page_dir) cpu_set_page(page_dir)

#define context_fn(context) context->x7
#define context_ret(context) context->x0
#define context_set_entry(context, entry) (((interrupt_context_t*)context)->pc = entry);

#define context_restore(duck_context) interrupt_exit_context(duck_context->ksp);

int context_clone(context_t* des, context_t* src);
int context_init(context_t* context, u64* ksp_top, u64* usp_top, u64* entry,
                 u32 level, int cpu);
void context_dump(context_t* c);
void context_dump_interrupt(interrupt_context_t* ic);
void context_dump_fault(interrupt_context_t* context, u64 fault_addr);

#endif
