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
  u32 ra;  // Return address register
  u32 sp;  // Stack pointer register
  u32 gp;  // Global pointer register
  u32 tp;  // Thread pointer register

  u32 t0;  // Temporary register 0
  u32 t1;  // Temporary register 1
  u32 t2;  // Temporary register 2

  u32 a0;  // Argument/return value register 0
  u32 a1;  // Argument/return value register 1
  u32 a2;  // Argument register 2
  u32 a3;  // Argument register 3
  u32 a4;  // Argument register 4
  u32 a5;  // Argument register 5
  u32 a6;  // Argument register 6
  u32 a7;  // Argument register 7

  u32 s0;   // Saved register 0 (also known as fp, frame pointer)
  u32 s1;   // Saved register 1
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

#define interrupt_process(X) \
  asm volatile(              \
      "addi sp, sp, -48 \n"  \
      "sw ra, 0(sp) \n"      \
      "sw s0, 8(sp) \n"      \
      "sw s1, 16(sp) \n"     \
      "sw s2, 24(sp) \n"     \
      "sw s3, 32(sp) \n"     \
      "sw s4, 40(sp) \n"     \
      "call " #X             \
      "\n"                   \
      "lw ra, 0(sp) \n"      \
      "lw s0, 8(sp) \n"      \
      "lw s1, 16(sp) \n"     \
      "lw s2, 24(sp) \n"     \
      "lw s3, 32(sp) \n"     \
      "lw s4, 40(sp) \n"     \
      "addi sp, sp, 48 \n" :\
:)

#define interrupt_entering_code(VEC, CODE, TYPE) \
  asm volatile(                                  \
      "csrrw sp, sscratch, sp\n"                 \
      "addi sp, sp, -31*4\n"                     \
      "sw ra, 2*4(sp)\n"                         \
      "sw sp, 3*4(sp)\n"                         \
      "sw gp, 4*4(sp)\n"                         \
      "sw tp, 5*4(sp)\n"                         \
      "sw t0, 6*4(sp)\n"                         \
      "sw t1, 7*4(sp)\n"                         \
      "sw t2, 8*4(sp)\n"                         \
      "sw a0, 8*4(sp)\n"                         \
      "sw a1, 10*4(sp)\n"                        \
      "sw a2, 11*4(sp)\n"                        \
      "sw a3, 12*4(sp)\n"                        \
      "sw a4, 13*4(sp)\n"                        \
      "sw a5, 14*4(sp)\n"                        \
      "sw a6, 15*4(sp)\n"                        \
      "sw a7, 16*4(sp)\n"                        \
      "sw s0, 17*4(sp)\n"                        \
      "sw s1, 18*4(sp)\n"                        \
      "sw s2, 19*4(sp)\n"                        \
      "sw s3, 20*4(sp)\n"                        \
      "sw s4, 21*4(sp)\n"                        \
      "sw s5, 22*4(sp)\n"                        \
      "sw s6, 23*4(sp)\n"                        \
      "sw s7, 24*4(sp)\n"                        \
      "sw s8, 25*4(sp)\n"                        \
      "sw s9, 26*4(sp)\n"                        \
      "sw s10, 27*4(sp)\n"                       \
      "sw s11, 28*4(sp)\n"                       \
      "csrr a0, sepc \n"                         \
      "sw a0, 29*4(sp)\n"                        \
      "csrr a0, sstatus \n"                      \
      "sw a0, 30*4(sp)\n"                        \
      "csrr a0, sscratch\n"                      \
      "sw a0, 3*4(sp)\n"                         \
      "li a0, %0\n"                              \
      "sw a0, 0*4(sp)\n"                         \
      "li a0, %1\n"                              \
      "sw a0, 1*4(sp)\n"                         \
      "mv a0, sp\n"                              \
      :                                          \
      : "i"(VEC), "i"(CODE))

#define interrupt_exit_context(duck_context) \
  asm volatile(                              \
      "mv sp, %0\n"                          \
      "lw t0, 29*4(sp)\n"                    \
      "csrw sepc,t0\n"                       \
      "lw t0, 30*4(sp)\n"                    \
      "csrw sstatus,t0\n"                    \
      "lw ra, 2*4(sp)\n"                     \
      "lw t0, 3*4(sp)\n"                     \
      "csrw sscratch, t0\n"                  \
      "lw gp, 4*4(sp)\n"                     \
      "lw tp, 5*4(sp)\n"                     \
      "lw t0, 6*4(sp)\n"                     \
      "lw t1, 7*4(sp)\n"                     \
      "lw t2, 8*4(sp)\n"                     \
      "lw a0, 8*4(sp)\n"                     \
      "lw a1, 10*4(sp)\n"                    \
      "lw a2, 11*4(sp)\n"                    \
      "lw a3, 12*4(sp)\n"                    \
      "lw a4, 13*4(sp)\n"                    \
      "lw a5, 14*4(sp)\n"                    \
      "lw a6, 15*4(sp)\n"                    \
      "lw a7, 16*4(sp)\n"                    \
      "lw s0, 17*4(sp)\n"                    \
      "lw s1, 18*4(sp)\n"                    \
      "lw s2, 19*4(sp)\n"                    \
      "lw s3, 20*4(sp)\n"                    \
      "lw s4, 21*4(sp)\n"                    \
      "lw s5, 22*4(sp)\n"                    \
      "lw s6, 23*4(sp)\n"                    \
      "lw s7, 24*4(sp)\n"                    \
      "lw s8, 25*4(sp)\n"                    \
      "lw s9, 26*4(sp)\n"                    \
      "lw s10, 27*4(sp)\n"                   \
      "lw s11, 28*4(sp)\n"                   \
      "addi sp, sp, 31*4\n"                  \
      "csrrw sp, sscratch, sp\n"             \
      "sret\n"                               \
      :                                      \
      : "r"(duck_context->ksp)               \
      : "memory");

#define interrupt_exit_ret()     \
  asm volatile(                  \
      "mv sp, a0\n"              \
      "lw t0, 29*4(sp)\n"        \
      "csrw sepc,t0\n"           \
      "lw t0, 30*4(sp)\n"        \
      "csrw sstatus,t0\n"        \
      "lw ra, 2*4(sp)\n"         \
      "lw t0, 3*4(sp)\n"         \
      "csrw sscratch, t0\n"      \
      "lw gp, 4*4(sp)\n"         \
      "lw tp, 5*4(sp)\n"         \
      "lw t0, 6*4(sp)\n"         \
      "lw t1, 7*4(sp)\n"         \
      "lw t2, 8*4(sp)\n"         \
      "lw a0, 8*4(sp)\n"         \
      "lw a1, 10*4(sp)\n"        \
      "lw a2, 11*4(sp)\n"        \
      "lw a3, 12*4(sp)\n"        \
      "lw a4, 13*4(sp)\n"        \
      "lw a5, 14*4(sp)\n"        \
      "lw a6, 15*4(sp)\n"        \
      "lw a7, 16*4(sp)\n"        \
      "lw s0, 17*4(sp)\n"        \
      "lw s1, 18*4(sp)\n"        \
      "lw s2, 19*4(sp)\n"        \
      "lw s3, 20*4(sp)\n"        \
      "lw s4, 21*4(sp)\n"        \
      "lw s5, 22*4(sp)\n"        \
      "lw s6, 23*4(sp)\n"        \
      "lw s7, 24*4(sp)\n"        \
      "lw s8, 25*4(sp)\n"        \
      "lw s9, 26*4(sp)\n"        \
      "lw s10, 27*4(sp)\n"       \
      "lw s11, 28*4(sp)\n"       \
      "addi sp, sp, 31*4\n"      \
      "csrrw sp, sscratch, sp\n" \
      "sret\n"                   \
      :                          \
      :                          \
      : "memory");

#define interrupt_exit()         \
  asm volatile(                  \
      "lw t0, 29*4(sp)\n"        \
      "csrw sepc,t0\n"           \
      "lw t0, 30*4(sp)\n"        \
      "csrw sstatus,t0\n"        \
      "lw ra, 2*4(sp)\n"         \
      "lw t0, 3*4(sp)\n"         \
      "csrw sscratch, t0\n"      \
      "lw gp, 4*4(sp)\n"         \
      "lw tp, 5*4(sp)\n"         \
      "lw t0, 6*4(sp)\n"         \
      "lw t1, 7*4(sp)\n"         \
      "lw t2, 8*4(sp)\n"         \
      "lw a0, 8*4(sp)\n"         \
      "lw a1, 10*4(sp)\n"        \
      "lw a2, 11*4(sp)\n"        \
      "lw a3, 12*4(sp)\n"        \
      "lw a4, 13*4(sp)\n"        \
      "lw a5, 14*4(sp)\n"        \
      "lw a6, 15*4(sp)\n"        \
      "lw a7, 16*4(sp)\n"        \
      "lw s0, 17*4(sp)\n"        \
      "lw s1, 18*4(sp)\n"        \
      "lw s2, 19*4(sp)\n"        \
      "lw s3, 20*4(sp)\n"        \
      "lw s4, 21*4(sp)\n"        \
      "lw s5, 22*4(sp)\n"        \
      "lw s6, 23*4(sp)\n"        \
      "lw s7, 24*4(sp)\n"        \
      "lw s8, 25*4(sp)\n"        \
      "lw s9, 26*4(sp)\n"        \
      "lw s10, 27*4(sp)\n"       \
      "lw s11, 28*4(sp)\n"       \
      "addi sp, sp, 31*4\n"      \
      "csrrw sp, sscratch, sp\n" \
      "sret\n"                   \
      :                          \
      :                          \
      : "memory");

#define context_switch_page(ctx, page_dir) cpu_set_page(page_dir)

#define context_fn(context) context->a7
#define context_ret(context) context->a0

#define context_set_entry(context, entry) \
  (((interrupt_context_t*)context)->ra = entry);
#define context_restore(duck_context) interrupt_exit_context(duck_context);

int context_clone(context_t* context, context_t* src);
int context_init(context_t* context, u32* ksp_top, u32* usp_top, u32* entry,
                 u32 level, int cpu);
void context_dump(context_t* c);

#endif