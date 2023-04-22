/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef CONTEXT_H
#define CONTEXT_H

#include "arch/boot.h"
#include "libs/include/types.h"
#include "platform/platform.h"

typedef struct interrupt_context {
  // manual push
  u32 no;
  u32 code;

  // General-purpose registers
  u32 ra;   // Return address register
  u32 sp;   // Stack pointer register
  u32 gp;   // Global pointer register
  u32 tp;   // Thread pointer register
  u32 t0;   // Temporary register 0
  u32 t1;   // Temporary register 1
  u32 t2;   // Temporary register 2
  u32 s0;   // Saved register 0 (also known as fp, frame pointer)
  u32 s1;   // Saved register 1
  u32 a0;   // Argument/return value register 0
  u32 a1;   // Argument/return value register 1
  u32 a2;   // Argument register 2
  u32 a3;   // Argument register 3
  u32 a4;   // Argument register 4
  u32 a5;   // Argument register 5
  u32 a6;   // Argument register 6
  u32 a7;   // Argument register 7
  u32 s2;   // Saved register 2
  u32 s3;   // Saved register 3
  u32 s4;   // Saved register 4
  u32 s5;   // Saved register 5
  u32 s6;   // Saved register 6
  u32 s7;   // Saved register 7
  u32 s8;   // Saved register 8
  u32 s9;   // Saved register 9
  u32 s10;  // Saved register 10
  u32 s11;  // Saved register 11

  u32 sepc;     // Supervisor exception program counter
  u32 sstatus;  // Supervisor status register

  // Specialized registers
  // u32 mstatus;  // Machine status register
  // u32 mcause;   // Machine trap cause register
  // u32 mepc;     // Machine exception program counter
  // u32 mtval;    // Machine trap value register
  // u32 mip;      // Machine interrupt-pending register
  // u32 mie;      // Machine interrupt-enable register
  // u32 stvec;    // Supervisor trap vector register
  // u32 scause;   // Supervisor trap cause register
  // u32 stval;    // Supervisor trap value register
  // u32 sip;      // Supervisor interrupt-pending register
  // u32 sie;      // Supervisor interrupt-enable register
  // u32 satp;     // Supervisor address translation and protection register

} __attribute__((packed)) interrupt_context_t;

typedef struct context_t {
  interrupt_context_t* ic;
  interrupt_context_t* ksp;
  u32 usp;
  u32 eip;
  u32 level;
  u32 tid;

  u32 ksp_start;
  u32 ksp_end;

  u32 usp_size;
  u32 ksp_size;
} context_t;

#define interrupt_process(X)

#define interrupt_entering_code(VEC, CODE, TYPE) \
  asm volatile(                                  \
      "addi sp, sp, -112\n"                      \
      "sd ra, 104(sp)\n"                         \
      "sd sp, 96(sp)\n"                          \
      "sd gp, 88(sp)\n"                          \
      "sd tp, 80(sp)\n"                          \
      "sd t0, 72(sp)\n"                          \
      "sd t1, 64(sp)\n"                          \
      "sd t2, 56(sp)\n"                          \
      "sd s0, 48(sp)\n"                          \
      "sd s1, 40(sp)\n"                          \
      "sd a0, 32(sp)\n"                          \
      "sd a1, 24(sp)\n"                          \
      "sd a2, 16(sp)\n"                          \
      "sd a3, 8(sp)\n"                           \
      "mv a0, %0\n"                              \
      "mv a1, %1\n"                              \
      "mv a2, %2\n"                              \
      "mv a0, sp\n"                              \
      :                                          \
      : "i"(VEC), "i"(CODE), "i"(TYPE))

#define interrupt_exit_context(duck_context) \
  asm volatile(                              \
      "mv sp, %0\n"                          \
      "lw ra, 104(sp)\n"                     \
      "lw sp, 96(sp)\n"                      \
      "lw gp, 88(sp)\n"                      \
      "lw tp, 80(sp)\n"                      \
      "lw t0, 72(sp)\n"                      \
      "lw t1, 64(sp)\n"                      \
      "lw t2, 56(sp)\n"                      \
      "lw s0, 48(sp)\n"                      \
      "lw s1, 40(sp)\n"                      \
      "lw a0, 32(sp)\n"                      \
      "lw a1, 24(sp)\n"                      \
      "lw a2, 16(sp)\n"                      \
      "lw a3, 8(sp)\n"                       \
      "addi sp, sp, 112\n"                   \
      "mret\n"                               \
      :                                      \
      : "r"(duck_context->ksp)               \
      : "memory", "ra");

#define interrupt_exit_ret() asm volatile("\n" : :)

#define interrupt_exit() asm volatile("" : :)

#define context_switch_page(ctx,page_dir) cpu_set_page(page_dir)

#define context_fn(context) context->a7
#define context_ret(context) context->a0

#define context_set_entry(context, entry) (((interrupt_context_t*)context)->ra = entry);
#define context_restore(duck_context) interrupt_exit_context(duck_context);

int context_clone(context_t* context, context_t* src);
int context_init(context_t* context, u32* ksp_top, u32* usp_top, u32* entry,
                 u32 level, int cpu);
void context_dump(context_t* c);

#endif