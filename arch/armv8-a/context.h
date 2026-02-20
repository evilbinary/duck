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
// Stack layout (from high to low address):
//   sp+0:   no, code (reserved)
// Stack layout after save_context (stack grows down, lower addresses at top):
//   sp+0:   no, code
//   sp+16:  psr (SPSR_EL1), pc (ELR_EL1)
//   sp+32:  lr (X30), sp (SP_EL0)
//   sp+48:  x28, x29
//   ...     x26-x0 (each stp stores first reg at lower address)
//   sp+240: x0, x1 (x0 at lower address, x1 at higher address)
//
// Note: stp Rx, Ry stores Rx at lower address, Ry at higher address
// So stp x0, x1 means: [sp] = x0, [sp+8] = x1
typedef struct interrupt_context {
  // At sp+0, sp+8 (pushed last in save_context)
  u64 no;
  u64 code;
  
  // At sp+16, sp+24 (saved from SPSR_EL1, ELR_EL1 via stp x3, x4)
  u64 psr;        // PSTATE (from SPSR_EL1) at sp+16
  u64 pc;         // Exception return address (from ELR_EL1) at sp+24
  
  // At sp+32, sp+40 (from: stp lr, x2, [sp, #-16]! where x2=sp_el0)
  // stp stores first operand at lower address
  u64 lr;         // Link register (X30) at sp+32
  u64 sp;         // User stack pointer (SP_EL0) at sp+40
  
  // At sp+48 onwards (general registers, pushed first)
  // Order matches stp instructions in save_context
  // stp Rx, Ry means Rx is at lower address, Ry at higher address
  u64 x28;        // sp+48 (from stp x28, x29)
  u64 x29;        // sp+56
  u64 x26;        // sp+64 (from stp x26, x27)
  u64 x27;        // sp+72
  u64 x24;        // sp+80 (from stp x24, x25)
  u64 x25;        // sp+88
  u64 x22;        // sp+96 (from stp x22, x23)
  u64 x23;        // sp+104
  u64 x20;        // sp+112 (from stp x20, x21)
  u64 x21;        // sp+120
  u64 x18;        // sp+128 (from stp x18, x19)
  u64 x19;        // sp+136
  u64 x16;        // sp+144 (from stp x16, x17)
  u64 x17;        // sp+152
  u64 x14;        // sp+160 (from stp x14, x15)
  u64 x15;        // sp+168
  u64 x12;        // sp+176 (from stp x12, x13)
  u64 x13;        // sp+184
  u64 x10;        // sp+192 (from stp x10, x11)
  u64 x11;        // sp+200
  u64 x8;         // sp+208 (from stp x8, x9)
  u64 x9;         // sp+216
  u64 x6;         // sp+224 (from stp x6, x7)
  u64 x7;         // sp+232
  u64 x4;         // sp+240 (from stp x4, x5)
  u64 x5;         // sp+248
  u64 x2;         // sp+256 (from stp x2, x3)
  u64 x3;         // sp+264
  u64 x0;         // sp+272 (from stp x0, x1)
  u64 x1;         // sp+280
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
// interrupt_process calls the handler with x0 = interrupt_context_t*
// The handler returns the (possibly new) context pointer in x0
// x0-x18 are caller-saved in AAPCS64, so we need to preserve them if needed
#define interrupt_process(X) \
  asm volatile(              \
      "bl " #X              \
      "\n"                   \
      :                      \
      :                      \
      : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18", "x30", "memory")

// Save context on exception entry (matches armv7-a style)
// After this macro:
//   - All registers x0-x29, lr, sp_el0, spsr_el1, elr_el1 are saved on stack
//   - no, code are saved at top of stack
//   - sp points to interrupt_context_t on stack
//   - x0 = sp (pointer to interrupt_context_t)
// Note: TYPE parameter is kept for compatibility with armv7-a but not used in armv8
#define interrupt_entering_code(VEC, CODE, TYPE) \
  asm volatile(                                  \
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
      "mrs x2, sp_el0\n" \
      "stp lr, x2, [sp, #-16]!\n" \
      "mrs x3, spsr_el1\n" \
      "mrs x4, elr_el1\n" \
      "stp x3, x4, [sp, #-16]!\n" \
      "mov x0, %0\n" \
      "mov x1, %1\n" \
      "stp x0, x1, [sp, #-16]!\n" \
      "mov x0, sp\n" \
      :                                          \
      : "i"(VEC), "i"(CODE) \
      : "x0", "x1", "x2", "x3", "x4", "memory")

#define interrupt_exit_context(ksp) \
  asm volatile(                              \
      "mov sp, %0 \n"                         \
      "add sp, sp, #16\n"                    /* skip no, code */ \
      "ldp x0, x1, [sp], #16\n"              /* load psr, pc */ \
      "msr spsr_el1, x0\n"                   /* restore SPSR_EL1 */ \
      "msr elr_el1, x1\n"                    /* restore ELR_EL1 */ \
      "ldp lr, x0, [sp], #16\n"              /* load lr, sp_el0 */ \
      "msr sp_el0, x0\n"                     /* restore SP_EL0 */ \
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
      "eret\n" \
      :                                      \
      : "r"(ksp) \
      : "memory")

#define interrupt_exit()           \
  asm volatile(                    \
      "add sp, sp, #16\n"                    /* skip no, code */ \
      "ldp x0, x1, [sp], #16\n"              /* load psr, pc */ \
      "msr spsr_el1, x0\n"                   /* restore SPSR_EL1 */ \
      "msr elr_el1, x1\n"                    /* restore ELR_EL1 */ \
      "ldp lr, x0, [sp], #16\n"              /* load lr, sp_el0 */ \
      "msr sp_el0, x0\n"                     /* restore SP_EL0 */ \
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
      "eret\n" \
      :                            \
      :                            \
      : "memory")

#define interrupt_entering(VEC) interrupt_entering_code(VEC, 0)

// interrupt_exit_ret: restore context from x0 (returned by handler)
// This is used when handler returns a new context pointer (for context switch)
#define interrupt_exit_ret()       \
  asm volatile(                    \
      "mov sp, x0\n"               /* switch to new context stack */ \
      "add sp, sp, #16\n"          /* skip no, code */ \
      "ldp x0, x1, [sp], #16\n"    /* load psr, pc */ \
      "msr spsr_el1, x0\n"         /* restore SPSR_EL1 */ \
      "msr elr_el1, x1\n"          /* restore ELR_EL1 */ \
      "ldp lr, x0, [sp], #16\n"    /* load lr, sp_el0 */ \
      "msr sp_el0, x0\n"           /* restore SP_EL0 */ \
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
      "eret\n" \
      :                            \
      :                            \
      : "memory")

#define context_switch_page(ctx, page_dir) cpu_set_page(page_dir)

#define context_fn(context) context->x7
#define context_ret(context) context->x0
#define context_set_entry(context, entry) (((interrupt_context_t*)context)->pc = entry);

#define context_restore(duck_context) interrupt_exit_context(duck_context->ksp);

int context_clone(context_t* des, context_t* src);
int context_init(context_t* context, u64 ksp_top, u64 usp_top, u64 entry,
                 u32 level, int cpu);
void context_dump(context_t* c);
void context_dump_interrupt(interrupt_context_t* ic);
void context_dump_fault(interrupt_context_t* context, u64 fault_addr);

#endif
