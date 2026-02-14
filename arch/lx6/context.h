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

typedef struct interrupt_context {
  // manual push
  u32 no;    // 0
  u32 code;  // 1

  u32 ps;  // 2 Processor state

  u32 pc;   // 3 a0 return address
  u32 sp;   // 4 a1
  u32 a2;   // 5 arg0
  u32 a3;   // 6
  u32 a4;   // 7
  u32 a5;   // 8
  u32 a6;   // 9
  u32 a7;   // 10
  u32 a8;   // 11
  u32 a9;   // 12
  u32 a10;  // 13
  u32 a11;  // 14
  u32 a12;  // 15
  u32 a13;  // 16
  u32 a14;  // 17
  u32 a15;  // 18
  u32 sar;  // 19

  u32 exccause;  // 20 Exception Causes 84
  u32 excvaddr;  // 21 88

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
  asm volatile(\
      "call0    " #X         \
            "\n"                   \
      :                      \
      :)

// #define interrupt_entering_code(VEC, CODE) \
//         asm volatile(                            \
//             "addmi sp, sp, -21*4 \n"             \
//             "s32i a0,sp, 3*4 \n"                 \
//             "s32i a1,sp,4*4 \n"                  \
//             "s32i a2,sp,5*4 \n"                  \
//             "s32i a3,sp,6*4 \n"                  \
//             "s32i a4,sp,7*4 \n"                  \
//             "s32i a5,sp,8*4 \n"                  \
//             "s32i a6,sp,9*4 \n"                  \
//             "s32i a7,sp,10*4 \n"                 \
//             "s32i a8,sp,11*4 \n"                 \
//             "s32i a9,sp,12*4 \n"                 \
//             "s32i a10,sp,13*4 \n"                \
//             "s32i a11,sp,14*4 \n"                \
//             "s32i a12,sp,15*4 \n"                \
//             "s32i a13,sp,16*4 \n"                \
//             "s32i a14,sp,17*4 \n"                \
//             "s32i a15,sp,18*4 \n"                \
//             "rsr.ps a2\n"                        \
//             "s32i a2,sp, 2*4 \n"                 \
//             "rsr.epc1 a2 \n"                     \
//             "s32i a2,sp, 3*4 \n"                 \
//             "movi a2,%0 \n"                      \
//             "s32i a2,sp,0 \n"                    \
//             "movi a2,%1 \n"                      \
//             "s32i a2,sp,4 \n"                    \
//             :                                    \
//             : "i"(VEC), "i"(CODE))

// #define interrupt_exit_context(ksp) \
//         asm volatile(                     \
//             "l32i sp,%0\n"                \
//             "l32i a2,sp,2*4 \n"           \
//             "wsr a2,ps \n"                \
//             "l32i a2,sp,5*4 \n"           \
//             "l32i a3,sp,6*4 \n"           \
//             "l32i a4,sp,7*4\n"            \
//             "l32i a5,sp,8*4 \n"           \
//             "l32i a6,sp,9*4\n"            \
//             "l32i a7,sp,10*4 \n"          \
//             "l32i a8,sp,11*4 \n"          \
//             "l32i a9,sp,12*4 \n"          \
//             "l32i a10,sp,13*4 \n"         \
//             "l32i a11,sp,14*4 \n"         \
//             "l32i a12,sp,15*4 \n"         \
//             "l32i a13,sp,16*4 \n"         \
//             "l32i a14,sp,17*4 \n"         \
//             "l32i a15,sp,18*4 \n"         \
//             "l32i a0,sp,3*4 \n"           \
//             "wsr a0,epc1 \n"              \
//             "l32i a1,sp,4*4 \n"           \
//             "rfe\n"                       \
//             :                             \
//             : "m"(ksp))


#define interrupt_entering_code(VEC, CODE) \
    asm volatile( \
        "addi  sp, sp, -22 * 4 \n"          /* 分配 22 个 4 字节空间 */ \
        /* 手动保存字段（偏移 0-21） */ \
        "s32i  a0,  sp, 0 * 4 \n"           /* no (占位，可能由中断号填充) */ \
        "s32i  a0,  sp, 1 * 4 \n"           /* code (占位) */ \
        "rsr.ps a0 \n"                    \
        "s32i  a0,  sp, 2 * 4 \n"           /* ps (偏移 2) */ \
        "rsr.epc1 a0 \n"                  \
        "s32i  a0,  sp, 3 * 4 \n"           /* pc (偏移 3) */ \
        "s32i  a1,  sp, 4 * 4 \n"           /* sp (偏移 4) */ \
        "s32i  a2,  sp, 5 * 4 \n"           /* a2 (偏移 5) */ \
        "s32i  a3,  sp, 6 * 4 \n"           \
        "s32i  a4,  sp, 7 * 4 \n"           \
        "s32i  a5,  sp, 8 * 4 \n"           \
        "s32i  a6,  sp, 9 * 4 \n"           \
        "s32i  a7,  sp, 10 * 4 \n"          \
        "s32i  a8,  sp, 11 * 4 \n"          \
        "s32i  a9,  sp, 12 * 4 \n"          \
        "s32i  a10, sp, 13 * 4 \n"          \
        "s32i  a11, sp, 14 * 4 \n"          \
        "s32i  a12, sp, 15 * 4 \n"          \
        "s32i  a13, sp, 16 * 4 \n"          \
        "s32i  a14, sp, 17 * 4 \n"          \
        "s32i  a15, sp, 18 * 4 \n"          \
        "rsr.sar a0 \n"                   \
        "s32i  a0,  sp, 19 * 4 \n"          /* sar (偏移 19) */ \
        "rsr.exccause a0 \n"              \
        "s32i  a0,  sp, 20 * 4 \n"          /* exccause (偏移 20) */ \
        "rsr.excvaddr a0 \n"              \
        "s32i  a0,  sp, 21 * 4 \n"          /* excvaddr (偏移 21) */ \
        /* 填充 no 和 code 字段（根据参数） */ \
        "movi  a0, %0 \n"                 \
        "s32i  a0,  sp, 0 * 4 \n"           /* no = VEC */ \
        "movi  a0, %1 \n"                 \
        "s32i  a0,  sp, 1 * 4 \n"           /* code = CODE */ \
        :                                   \
        : "i"(VEC), "i"(CODE)              \
        : "a0", "memory"                   \
    )


#define interrupt_exit_context(ksp) \
    asm volatile( \
        "mov   sp, %0 \n"                  /* 恢复栈指针 */ \
        /* 恢复字段（偏移 0-21） */ \
        "l32i  a0,  sp, 2 * 4 \n"            \
        "wsr   a0, ps \n"                 /* 恢复 PS */ \
        "l32i  a0,  sp, 3 * 4 \n"            \
        "wsr   a0, epc1 \n"               /* 恢复 PC */ \
        "l32i  a1,  sp, 4 * 4 \n"            \
        "l32i  a2,  sp, 5 * 4 \n"            \
        "l32i  a3,  sp, 6 * 4 \n"            \
        "l32i  a4,  sp, 7 * 4 \n"            \
        "l32i  a5,  sp, 8 * 4 \n"            \
        "l32i  a6,  sp, 9 * 4 \n"            \
        "l32i  a7,  sp, 10 * 4 \n"           \
        "l32i  a8,  sp, 11 * 4 \n"           \
        "l32i  a9,  sp, 12 * 4 \n"           \
        "l32i  a10, sp, 13 * 4 \n"           \
        "l32i  a11, sp, 14 * 4 \n"           \
        "l32i  a12, sp, 15 * 4 \n"           \
        "l32i  a13, sp, 16 * 4 \n"           \
        "l32i  a14, sp, 17 * 4 \n"           \
        "l32i  a15, sp, 18 * 4 \n"           \
        "l32i  a0,  sp, 19 * 4 \n"           \
        "wsr   a0, sar \n"                /* 恢复 SAR */ \
        "addi  sp, sp, 22 * 4 \n"           /* 恢复原始栈指针 */ \
        "rfe \n"                           \
        :                                   \
        : "r"(ksp)                         \
        : "a0", "memory"                   \
    )


#define interrupt_entering(VEC) interrupt_entering_code(VEC, 0)

#define interrupt_exit()

#define interrupt_exit2()

#define context_switch_page(ctx, page_dir) \
        cpu_set_page(                            \
            page_dir)  // asm volatile("mov %0, %%cr3" : : "r" (page_dir))

#define context_fn(context) context->a7
#define context_ret(context) context->a2
#define context_set_entry(context, entry) \
        ((interrupt_context_t*)((context)));

#define context_restore(duck_context) \
        interrupt_exit_context(duck_context->ksp);

#endif