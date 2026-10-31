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
/* ---------------------------------------------------------------------------
 * 浮点/SIMD 现场（q0-q31 + fpsr + fpcr，共 528 字节）的存取原语
 *
 * 【放哪里由调度层决定，不放进异常帧】q/fpsr/fpcr 是"每核"而不是"每线程"的
 * 寄存器，异常进入时硬件不压。但"每次异常都压"是浪费：syscall/缺页/定时器
 * 绝大多数并不换线程，而真正换线程时只需要一次搬运。所以现场放在
 * thread_t.fp（每线程一块），由 thread_fp_switch() 在【真正切换】时
 * save 旧线程 / load 新线程 —— 与 arm64 Linux 的
 * fpsimd_thread_switch() + thread_struct.uw.fpsimd_state 同一思路。
 *
 * 为什么不用 CPACR_EL1.FPEN 陷阱做"用到了才搬"（x86 CR0.TS / arm32 FPEXC.EN
 * 那一套）：本移植的用户线程跑在 EL1t（绕 PXN），而 FPEN 是 EL0/EL1 两个
 * bit 分开管的，"用户 FP"在本平台就是 EL1 FP，无法与内核自身区分 ⇒ 陷阱法
 * 不可用。
 *
 * 布局（相对基址）：q0..q31 = 0/16/…/496，fpsr = 512，fpcr = 520。
 * 基址必须是 16 字节对齐（thread_t 由 kmalloc 给出，按 16 对齐；这里仍用
 * 单条 stp/ldp，SCTLR_EL1.A=0 时非对齐访问也可用）。
 * ---------------------------------------------------------------------------
 */
#define __fp_state_save(mem)                              \
  asm volatile(                                           \
      "stp  q0,  q1,  [%0, #0]\n"                         \
      "stp  q2,  q3,  [%0, #32]\n"                        \
      "stp  q4,  q5,  [%0, #64]\n"                        \
      "stp  q6,  q7,  [%0, #96]\n"                        \
      "stp  q8,  q9,  [%0, #128]\n"                       \
      "stp  q10, q11, [%0, #160]\n"                       \
      "stp  q12, q13, [%0, #192]\n"                       \
      "stp  q14, q15, [%0, #224]\n"                       \
      "stp  q16, q17, [%0, #256]\n"                       \
      "stp  q18, q19, [%0, #288]\n"                       \
      "stp  q20, q21, [%0, #320]\n"                       \
      "stp  q22, q23, [%0, #352]\n"                       \
      "stp  q24, q25, [%0, #384]\n"                       \
      "stp  q26, q27, [%0, #416]\n"                       \
      "stp  q28, q29, [%0, #448]\n"                       \
      "stp  q30, q31, [%0, #480]\n"                       \
      "mrs  x9,  fpsr\n"                                  \
      "str  x9,  [%0, #512]\n"                            \
      "mrs  x9,  fpcr\n"                                  \
      "str  x9,  [%0, #520]\n"                            \
      :                                                   \
      : "r"(mem)                                          \
      : "memory", "x9")

#define __fp_state_load(mem)                              \
  asm volatile(                                           \
      "ldp  q0,  q1,  [%0, #0]\n"                         \
      "ldp  q2,  q3,  [%0, #32]\n"                        \
      "ldp  q4,  q5,  [%0, #64]\n"                        \
      "ldp  q6,  q7,  [%0, #96]\n"                        \
      "ldp  q8,  q9,  [%0, #128]\n"                       \
      "ldp  q10, q11, [%0, #160]\n"                       \
      "ldp  q12, q13, [%0, #192]\n"                       \
      "ldp  q14, q15, [%0, #224]\n"                       \
      "ldp  q16, q17, [%0, #256]\n"                       \
      "ldp  q18, q19, [%0, #288]\n"                       \
      "ldp  q20, q21, [%0, #320]\n"                       \
      "ldp  q22, q23, [%0, #352]\n"                       \
      "ldp  q24, q25, [%0, #384]\n"                       \
      "ldp  q26, q27, [%0, #416]\n"                       \
      "ldp  q28, q29, [%0, #448]\n"                       \
      "ldp  q30, q31, [%0, #480]\n"                       \
      "ldr  x9,  [%0, #512]\n"                            \
      "msr  fpsr, x9\n"                                   \
      "ldr  x9,  [%0, #520]\n"                            \
      "msr  fpcr, x9\n"                                   \
      :                                                   \
      : "r"(mem)                                          \
      : "memory", "x9")

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
      "add  sp,  x0, #0\n"       /* sp = x0 (ic ptr)*/ \
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
// interrupt_exit_ret_safe: like interrupt_exit_ret(), but tolerates handlers
// that return a non-pointer value in x0 (e.g. syscall returning fd=2).
//
// If x0 doesn't look like a valid interrupt_context_t* (too small or not 16B
// aligned), fall back to the current sp (which already points at ic after
// interrupt_entering_code).
// ---------------------------------------------------------------------------
#define interrupt_exit_ret_safe()                       \
  asm volatile(                                         \
      "mov  x9, x0\n"                                   \
      "tst  x9, #0xF\n"                                 \
      "b.ne 1f\n"                                       \
      "cmp  x9, #0x1000\n"                              \
      "b.lo 1f\n"                                       \
      "b    2f\n"                                       \
      "1: mov x9, sp\n"                                 \
      "2: add sp, x9, #0\n"                             \
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
      : "memory", "x9")

#define interrupt_entering(VEC) interrupt_entering_code(VEC, 0, 0)

// Syscall number is in x8 (Linux AArch64 ABI)
#define context_fn(context)  ((context)->x8)
#define context_ret(context) ((context)->x0)
#define context_arg0(context) ((context)->x0)
#define context_arg1(context) ((context)->x1)
#define context_arg2(context) ((context)->x2)
#define context_arg3(context) ((context)->x3)
#define context_arg4(context) ((context)->x4)
#define context_arg5(context) ((context)->x5)
#define context_set_entry(context, entry)                \
  do {                                                   \
    ((interrupt_context_t*)(context))->pc = (u64)(entry); \
    ((interrupt_context_t*)(context))->lr = (u64)(entry); \
  } while (0)

#define context_exec_live(ctx) ((ctx)->ksp)

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
