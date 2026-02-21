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

// ARM64 interrupt context — matches the save/restore order in
// interrupt_entering_code / interrupt_exit exactly.
//
// Save order (stack grows down, last pushed = lowest address = sp+0):
//   stp x0,x1    → pushed first  → highest address
//   stp x2,x3
//   ...
//   stp x28,x29
//   stp lr, sp_el0
//   stp spsr, elr
//   stp no, code  → pushed last   → sp+0
//
// So the struct layout from low→high address is:
typedef struct interrupt_context {
  u64 no;    // sp+0
  u64 code;  // sp+8

  u64 psr;   // sp+16  (SPSR_EL1)
  u64 pc;    // sp+24  (ELR_EL1)

  u64 lr;    // sp+32  (X30)
  u64 sp;    // sp+40  (SP_EL0)

  u64 x28;   // sp+48
  u64 x29;   // sp+56
  u64 x26;   // sp+64
  u64 x27;   // sp+72
  u64 x24;   // sp+80
  u64 x25;   // sp+88
  u64 x22;   // sp+96
  u64 x23;   // sp+104
  u64 x20;   // sp+112
  u64 x21;   // sp+120
  u64 x18;   // sp+128
  u64 x19;   // sp+136
  u64 x16;   // sp+144
  u64 x17;   // sp+152
  u64 x14;   // sp+160
  u64 x15;   // sp+168
  u64 x12;   // sp+176
  u64 x13;   // sp+184
  u64 x10;   // sp+192
  u64 x11;   // sp+200
  u64 x8;    // sp+208
  u64 x9;    // sp+216
  u64 x6;    // sp+224
  u64 x7;    // sp+232
  u64 x4;    // sp+240
  u64 x5;    // sp+248
  u64 x2;    // sp+256
  u64 x3;    // sp+264
  u64 x0;    // sp+272
  u64 x1;    // sp+280
} __attribute__((packed)) interrupt_context_t;  // sizeof = 288

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

// ---------------------------------------------------------------------------
// interrupt_process: call handler with x0 = ic pointer
// ---------------------------------------------------------------------------
#define interrupt_process(X) \
  asm volatile(              \
      "bl " #X "\n"          \
      :                      \
      :                      \
      : "memory")

// ---------------------------------------------------------------------------
// interrupt_entering_code: save all registers onto SP_EL1 stack
//   VEC  → ic->no
//   CODE → ic->code
// ---------------------------------------------------------------------------
#define interrupt_entering_code(VEC, CODE, TYPE)        \
  asm volatile(                                         \
      "stp x0,  x1,  [sp, #-16]!\n"                    \
      "stp x2,  x3,  [sp, #-16]!\n"                    \
      "stp x4,  x5,  [sp, #-16]!\n"                    \
      "stp x6,  x7,  [sp, #-16]!\n"                    \
      "stp x8,  x9,  [sp, #-16]!\n"                    \
      "stp x10, x11, [sp, #-16]!\n"                    \
      "stp x12, x13, [sp, #-16]!\n"                    \
      "stp x14, x15, [sp, #-16]!\n"                    \
      "stp x16, x17, [sp, #-16]!\n"                    \
      "stp x18, x19, [sp, #-16]!\n"                    \
      "stp x20, x21, [sp, #-16]!\n"                    \
      "stp x22, x23, [sp, #-16]!\n"                    \
      "stp x24, x25, [sp, #-16]!\n"                    \
      "stp x26, x27, [sp, #-16]!\n"                    \
      "stp x28, x29, [sp, #-16]!\n"                    \
      "mrs x2,  sp_el0\n"                               \
      "stp x30, x2,  [sp, #-16]!\n"  /* lr, sp_el0 */  \
      "mrs x3,  spsr_el1\n"                             \
      "mrs x4,  elr_el1\n"                              \
      "stp x3,  x4,  [sp, #-16]!\n"  /* psr, pc */     \
      "mov x3,  %0\n"                                   \
      "mov x4,  %1\n"                                   \
      "stp x3,  x4,  [sp, #-16]!\n"  /* no, code */    \
      "mov x0,  sp\n"                 /* x0 = ic ptr */ \
      :                                                 \
      : "i"(VEC), "i"(CODE)                             \
      : "memory")

// ---------------------------------------------------------------------------
// interrupt_exit_context: restore from a saved ksp pointer (first run)
//   Same as armv7-a interrupt_exit_context: load sp from the pointer,
//   then restore all fields.
// ---------------------------------------------------------------------------
#define interrupt_exit_context(ksp)                     \
  asm volatile(                                         \
      "mov  sp,  %0\n"           /* sp = ic address */  \
      "add  sp,  sp, #16\n"      /* skip no, code   */  \
      "ldp  x0,  x1,  [sp], #16\n"                      \
      "msr  spsr_el1, x0\n"                              \
      "msr  elr_el1,  x1\n"                              \
      "ldp  x30, x0,  [sp], #16\n"                       \
      "msr  sp_el0,   x0\n"                              \
      "ldp  x28, x29, [sp], #16\n"                       \
      "ldp  x26, x27, [sp], #16\n"                       \
      "ldp  x24, x25, [sp], #16\n"                       \
      "ldp  x22, x23, [sp], #16\n"                       \
      "ldp  x20, x21, [sp], #16\n"                       \
      "ldp  x18, x19, [sp], #16\n"                       \
      "ldp  x16, x17, [sp], #16\n"                       \
      "ldp  x14, x15, [sp], #16\n"                       \
      "ldp  x12, x13, [sp], #16\n"                       \
      "ldp  x10, x11, [sp], #16\n"                       \
      "ldp  x8,  x9,  [sp], #16\n"                       \
      "ldp  x6,  x7,  [sp], #16\n"                       \
      "ldp  x4,  x5,  [sp], #16\n"                       \
      "ldp  x2,  x3,  [sp], #16\n"                       \
      "ldp  x0,  x1,  [sp], #16\n"                       \
      "eret\n"                                            \
      :                                                  \
      : "r"(ksp)                                         \
      : "memory")

// ---------------------------------------------------------------------------
// interrupt_exit: restore from current sp (same thread, same EL)
// ---------------------------------------------------------------------------
#define interrupt_exit()                                \
  asm volatile(                                         \
      "add  sp,  sp, #16\n"      /* skip no, code   */  \
      "ldp  x0,  x1,  [sp], #16\n"                      \
      "msr  spsr_el1, x0\n"                              \
      "msr  elr_el1,  x1\n"                              \
      "ldp  x30, x0,  [sp], #16\n"                       \
      "msr  sp_el0,   x0\n"                              \
      "ldp  x28, x29, [sp], #16\n"                       \
      "ldp  x26, x27, [sp], #16\n"                       \
      "ldp  x24, x25, [sp], #16\n"                       \
      "ldp  x22, x23, [sp], #16\n"                       \
      "ldp  x20, x21, [sp], #16\n"                       \
      "ldp  x18, x19, [sp], #16\n"                       \
      "ldp  x16, x17, [sp], #16\n"                       \
      "ldp  x14, x15, [sp], #16\n"                       \
      "ldp  x12, x13, [sp], #16\n"                       \
      "ldp  x10, x11, [sp], #16\n"                       \
      "ldp  x8,  x9,  [sp], #16\n"                       \
      "ldp  x6,  x7,  [sp], #16\n"                       \
      "ldp  x4,  x5,  [sp], #16\n"                       \
      "ldp  x2,  x3,  [sp], #16\n"                       \
      "ldp  x0,  x1,  [sp], #16\n"                       \
      "eret\n"                                            \
      :                                                  \
      :                                                  \
      : "memory")

// ---------------------------------------------------------------------------
// interrupt_exit_ret: restore from x0 (ic pointer returned by handler)
//   Mirrors armv7-a: "mov sp, r0" then restore.
//   Used after IRQ/context-switch so SP_EL1 is set to the ic location
//   of whichever thread we return to (same or next).
// ---------------------------------------------------------------------------
#define interrupt_exit_ret()                            \
  asm volatile(                                         \
      "mov  sp,  x0\n"           /* sp = returned ic */ \
      "add  sp,  sp, #16\n"      /* skip no, code   */  \
      "ldp  x0,  x1,  [sp], #16\n"                      \
      "msr  spsr_el1, x0\n"                              \
      "msr  elr_el1,  x1\n"                              \
      "ldp  x30, x0,  [sp], #16\n"                       \
      "msr  sp_el0,   x0\n"                              \
      "ldp  x28, x29, [sp], #16\n"                       \
      "ldp  x26, x27, [sp], #16\n"                       \
      "ldp  x24, x25, [sp], #16\n"                       \
      "ldp  x22, x23, [sp], #16\n"                       \
      "ldp  x20, x21, [sp], #16\n"                       \
      "ldp  x18, x19, [sp], #16\n"                       \
      "ldp  x16, x17, [sp], #16\n"                       \
      "ldp  x14, x15, [sp], #16\n"                       \
      "ldp  x12, x13, [sp], #16\n"                       \
      "ldp  x10, x11, [sp], #16\n"                       \
      "ldp  x8,  x9,  [sp], #16\n"                       \
      "ldp  x6,  x7,  [sp], #16\n"                       \
      "ldp  x4,  x5,  [sp], #16\n"                       \
      "ldp  x2,  x3,  [sp], #16\n"                       \
      "ldp  x0,  x1,  [sp], #16\n"                       \
      "eret\n"                                            \
      :                                                  \
      :                                                  \
      : "memory")

#define interrupt_entering(VEC) interrupt_entering_code(VEC, 0, 0)

// Syscall number is in x8 (Linux AArch64 ABI)
#define context_fn(context)  ((context)->x8)
#define context_ret(context) ((context)->x0)
#define context_set_entry(context, entry) \
  (((interrupt_context_t*)(context))->pc = (u64)(entry))

void context_switch_page(context_t* ctx, u64 page_dir);

// context_restore: used by kernel_run() for the very first eret into a thread.
// Equivalent to armv7-a: load sp from ksp pointer then restore all regs.
#define context_restore(duck_context) \
  interrupt_exit_context((duck_context)->ksp)

int  context_clone(context_t* des, context_t* src);
int  context_init(context_t* context, u64 ksp_top, u64 usp_top, u64 entry,
                  u32 level, int cpu);
void context_dump(context_t* c);
void context_dump_interrupt(interrupt_context_t* ic);
void context_dump_fault(interrupt_context_t* context, u64 fault_addr);

#endif
