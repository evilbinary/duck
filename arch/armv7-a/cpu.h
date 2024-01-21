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

typedef u32 (*sys_call_fn)(u32 arg1, u32 arg2, u32 arg3, u32 arg4, u32 arg5,
                           u32 arg6);

#define sys_fn_call(duck_interrupt_context, fn)               \
  duck_interrupt_context->r0 = ((sys_call_fn)fn)(             \
      duck_interrupt_context->r0, duck_interrupt_context->r1, \
      duck_interrupt_context->r2, duck_interrupt_context->r3, \
      duck_interrupt_context->r4, duck_interrupt_context->r5);

#define cpu_cli() asm("cpsid if" : : : "memory", "cc")
#define cpu_sti() asm("cpsie if" : : : "memory", "cc")
#define cpu_cpl() (cpu_get_cs() & 0x3)

#define isb() asm volatile("isb sy")
#define dsb() asm volatile("dsb sy")
#define dmb() asm volatile("dmb sy")

#define cpu_faa(ptr) __sync_fetch_and_add(ptr, 1)

#define syscall0(syscall_num)    \
  ({                             \
    int ret;                     \
    asm volatile(                \
        "mov r7, %1\n"           \
        "svc 0\n"                \
        "mov %0, r0\n"           \
        : "=r"(ret)              \
        : "r"(syscall_num)       \
        : "r0", "r7", "memory"); \
    ret;                         \
  })

#define syscall1(syscall_num, arg1)   \
  ({                                  \
    int ret;                          \
    asm volatile(                     \
        "mov r7, %1\n"                \
        "mov r0, %2\n"                \
        "svc 0\n"                     \
        "mov %0, r0\n"                \
        : "=r"(ret)                   \
        : "r"(syscall_num), "r"(arg1) \
        : "r0", "r7", "memory");      \
    ret;                              \
  })

#define syscall2(syscall_num, arg1, arg2)        \
  ({                                             \
    int ret;                                     \
    asm volatile(                                \
        "mov r7, %1\n"                           \
        "mov r0, %2\n"                           \
        "mov r1, %3\n"                           \
        "svc 0\n"                                \
        "mov %0, r0\n"                           \
        : "=r"(ret)                              \
        : "r"(syscall_num), "r"(arg1), "r"(arg2) \
        : "r0", "r1", "r7", "memory");           \
    ret;                                         \
  })

#define syscall3(syscall_num, arg1, arg2, arg3)             \
  ({                                                        \
    int ret;                                                \
    asm volatile(                                           \
        "mov r7, %1\n"                                      \
        "mov r0, %2\n"                                      \
        "mov r1, %3\n"                                      \
        "mov r2, %4\n"                                      \
        "svc 0\n"                                           \
        "mov %0, r0\n"                                      \
        : "=r"(ret)                                         \
        : "r"(syscall_num), "r"(arg1), "r"(arg2), "r"(arg3) \
        : "r0", "r1", "r2", "r7", "memory");                \
    ret;                                                    \
  })

#define syscall4(syscall_num, arg1, arg2, arg3, arg4)                  \
  ({                                                                   \
    int ret;                                                           \
    asm volatile(                                                      \
        "mov r7, %1\n"                                                 \
        "mov r0, %2\n"                                                 \
        "mov r1, %3\n"                                                 \
        "mov r2, %4\n"                                                 \
        "mov r3, %5\n"                                                 \
        "svc 0\n"                                                      \
        "mov %0, r0\n"                                                 \
        : "=r"(ret)                                                    \
        : "r"(syscall_num), "r"(arg1), "r"(arg2), "r"(arg3), "r"(arg4) \
        : "r0", "r1", "r2", "r3", "r7", "memory");                     \
    ret;                                                               \
  })

#define syscall5(syscall_num, arg1, arg2, arg3, arg4, arg5)             \
  ({                                                                    \
    int ret;                                                            \
    asm volatile(                                                       \
        "mov r7, %1\n"                                                  \
        "mov r0, %2\n"                                                  \
        "mov r1, %3\n"                                                  \
        "mov r2, %4\n"                                                  \
        "mov r3, %5\n"                                                  \
        "mov r4, %6\n"                                                  \
        "svc 0\n"                                                       \
        "mov %0, r0\n"                                                  \
        : "=r"(ret)                                                     \
        : "r"(syscall_num), "r"(arg1), "r"(arg2), "r"(arg3), "r"(arg4), \
          "r"(arg5)                                                     \
        : "r0", "r1", "r2", "r3", "r4", "r7", "memory");                \
    ret;                                                                \
  })

#endif