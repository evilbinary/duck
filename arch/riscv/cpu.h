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

#define CSR_CYCLE		0xc00
#define CSR_TIME		0xc01
#define CSR_INSTRET		0xc02
#define CSR_SSTATUS		0x100
#define CSR_SIE			0x104
#define CSR_STVEC		0x105
#define CSR_SCOUNTEREN		0x106
#define CSR_SSCRATCH		0x140
#define CSR_SEPC		0x141
#define CSR_SCAUSE		0x142
#define CSR_STVAL		0x143
#define CSR_SIP			0x144
#define CSR_SATP		0x180
#define CSR_CYCLEH		0xc80
#define CSR_TIMEH		0xc81
#define CSR_INSTRETH		0xc82


#define __ASM_STR(x)	#x

#define cpu_csr_read(csr)						\
({								\
	register unsigned long __v;				\
	__asm__ __volatile__ ("csrr %0, " __ASM_STR(csr)	\
			      : "=r" (__v) :			\
			      : "memory");			\
	__v;							\
})

#define cpu_csr_write(csr, val)					\
({								\
	unsigned long __v = (unsigned long)(val);		\
	__asm__ __volatile__ ("csrw " __ASM_STR(csr) ", %0"	\
			      : : "rK" (__v)			\
			      : "memory");			\
})

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
  duck_interrupt_context->a0 = ((sys_call_fn)fn)(             \
      duck_interrupt_context->a0, duck_interrupt_context->a1, \
      duck_interrupt_context->a2, duck_interrupt_context->a3, \
      duck_interrupt_context->a4, duck_interrupt_context->a5);

#define cpu_cli() asm("csrsi   sstatus,2\n")
#define cpu_sti() asm("csrci   sstatus,2\n")
#define cpu_cpl() (cpu_get_cs() & 0x3)

#define syscall0(syscall_num)    \
  ({                             \
    int ret;                     \
    asm volatile(                \
        "li a7, %1\n"            \
        "ecall\n"                \
        "mv %0, a0\n"            \
        : "=r"(ret)              \
        : "i"(syscall_num)       \
        : "a0", "a7", "memory"); \
    ret;                         \
  })

#define syscall1(syscall_num, arg1)   \
  ({                                  \
    int ret;                          \
    asm volatile(                     \
        "li a7, %1\n"                 \
        "mv a0, %2\n"                 \
        "ecall\n"                     \
        "mv %0, a0\n"                 \
        : "=r"(ret)                   \
        : "i"(syscall_num), "r"(arg1) \
        : "a0", "a7", "memory");      \
    ret;                              \
  })

#define syscall2(syscall_num, arg1, arg2)        \
  ({                                             \
    int ret;                                     \
    asm volatile(                                \
        "li a7, %1\n"                            \
        "mv a0, %2\n"                            \
        "mv a1, %3\n"                            \
        "ecall\n"                                \
        "mv %0, a0\n"                            \
        : "=r"(ret)                              \
        : "i"(syscall_num), "r"(arg1), "r"(arg2) \
        : "a0", "a1", "a7", "memory");           \
    ret;                                         \
  })

#define syscall3(syscall_num, arg1, arg2, arg3)             \
  ({                                                        \
    int ret;                                                \
    asm volatile(                                           \
        "li a7, %1\n"                                       \
        "mv a0, %2\n"                                       \
        "mv a1, %3\n"                                       \
        "mv a2, %4\n"                                       \
        "ecall\n"                                           \
        "mv %0, a0\n"                                       \
        : "=r"(ret)                                         \
        : "i"(syscall_num), "r"(arg1), "r"(arg2), "r"(arg3) \
        : "a0", "a1", "a2", "a7", "memory");                \
    ret;                                                    \
  })

#define syscall4(syscall_num, arg1, arg2, arg3, arg4)                  \
  ({                                                                   \
    int ret;                                                           \
    asm volatile(                                                      \
        "li a7, %1\n"                                                  \
        "mv a0, %2\n"                                                  \
        "mv a1, %3\n"                                                  \
        "mv a2, %4\n"                                                  \
        "mv a3, %5\n"                                                  \
        "ecall\n"                                                      \
        "mv %0, a0\n"                                                  \
        : "=r"(ret)                                                    \
        : "i"(syscall_num), "r"(arg1), "r"(arg2), "r"(arg3), "r"(arg4) \
        : "a0", "a1", "a2", "a3", "a7", "memory");                     \
    ret;                                                               \
  })

#define syscall5(syscall_num, arg1, arg2, arg3, arg4, arg5)             \
  ({                                                                    \
    int ret;                                                            \
    asm volatile(                                                       \
        "li a7, %1\n"                                                   \
        "mv a0, %2\n"                                                   \
        "mv a1, %3\n"                                                   \
        "mv a2, %4\n"                                                   \
        "mv a3, %5\n"                                                   \
        "mv a4, %6\n"                                                   \
        "ecall\n"                                                       \
        "mv %0, a0\n"                                                   \
        : "=r"(ret)                                                     \
        : "i"(syscall_num), "r"(arg1), "r"(arg2), "r"(arg3), "r"(arg4), \
          "r"(arg5)                                                     \
        : "a0", "a1", "a2", "a3", "a4", "a7", "memory");                \
    ret;                                                                \
  })

#endif