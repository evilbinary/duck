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

// ARM64 PSTATE bits
typedef struct pstate {
  union {
    struct {
      u32 SP : 1;        // Stack pointer select (0=SP_EL0, 1=SP_ELx)
      u32 RESERVED1 : 1;
      u32 RESERVED2 : 1;
      u32 RESERVED3 : 1;
      u32 M : 4;         // Exception level (0=EL0t, 4=EL1t, 5=EL1h, 8=EL2t, 9=EL2h)
      u32 RESERVED4 : 4;
      u32 D : 1;         // Debug exception mask
      u32 A : 1;         // SError interrupt mask
      u32 I : 1;         // IRQ interrupt mask
      u32 F : 1;         // FIQ interrupt mask
      u32 RESERVED5 : 2;
      u32 SS : 1;        // Software step
      u32 IL : 1;        // Illegal execution state
      u32 RESERVED6 : 2;
      u32 V : 1;         // Overflow condition flag
      u32 C : 1;         // Carry condition flag
      u32 Z : 1;         // Zero condition flag
      u32 N : 1;         // Negative condition flag
      u32 RESERVED7 : 4;
    };
    u32 val;
  };
} __attribute__((packed)) pstate_t;

typedef u64 (*sys_call_fn)(u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5,
                           u64 arg6);

#define sys_fn_call(duck_interrupt_context, fn)                               \
  duck_interrupt_context->x0 = ((                                             \
      sys_call_fn)fn)(duck_interrupt_context->x0, duck_interrupt_context->x1, \
                      duck_interrupt_context->x2, duck_interrupt_context->x3, \
                      duck_interrupt_context->x4, duck_interrupt_context->x5);

// ARM64 interrupt disable/enable
#define cpu_cli() asm volatile("msr daifset, #0xF" : : : "memory")
#define cpu_sti() asm volatile("msr daifclr, #0xF" : : : "memory")
#define cpu_cpl() (cpu_get_cs() & 0x3)

// Memory barriers
#define isb() asm volatile("isb" : : : "memory")
#define dsb() asm volatile("dsb sy" : : : "memory")
#define dmb() asm volatile("dmb sy" : : : "memory")

// Atomic fetch and add
static inline int cpu_faa(volatile int* ptr) {
  int oldval, newval = 1;
  int result;
  asm volatile(
      "1: ldaxr %w0, [%2]\n"
      "add %w0, %w0, %w3\n"
      "stlxr %w1, %w0, [%2]\n"
      "cbnz %w1, 1b\n"
      : "=&r"(oldval), "=&r"(result)
      : "r"(ptr), "r"(newval)
      : "memory"
  );
  return oldval;
}

// Read system registers
static inline u64 read_cntfrq(void) {
  u64 val;
  asm volatile("mrs %0, cntfrq_el0" : "=r"(val));
  return val;
}

static inline u64 read_cntvct(void) {
  u64 val;
  asm volatile("mrs %0, cntvct_el0" : "=r"(val));
  return val;
}

static inline u64 read_cntv_tval(void) {
  u64 val;
  asm volatile("mrs %0, cntv_tval_el0" : "=r"(val));
  return val;
}

static inline void write_cntv_tval(u64 val) {
  asm volatile("msr cntv_tval_el0, %0" : : "r"(val) : "memory");
}

static inline void enable_cntv(int enable) {
  u64 val;
  asm volatile("mrs %0, cntv_ctl_el0" : "=r"(val));
  if (enable)
    val |= 1;
  else
    val &= ~1;
  asm volatile("msr cntv_ctl_el0, %0" : : "r"(val) : "memory");
}

// Read ESR (Exception Syndrome Register)
static inline u64 read_esr(void) {
  u64 val;
  asm volatile("mrs %0, esr_el1" : "=r"(val));
  return val;
}

// Read FAR (Fault Address Register)
static inline u64 read_far(void) {
  u64 val;
  asm volatile("mrs %0, far_el1" : "=r"(val));
  return val;
}

// Read current PC (approximate)
static inline u64 read_pc(void) {
  u64 val;
  asm volatile("adr %0, ." : "=r"(val));
  return val;
}

// Read frame pointer
static inline u64 read_fp(void) {
  u64 val;
  asm volatile("mov %0, x29" : "=r"(val));
  return val;
}

// Read current EL
static inline u64 read_current_el(void) {
  u64 val;
  asm volatile("mrs %0, CurrentEL" : "=r"(val));
  return val >> 2;
}

// System call macros for ARM64
#define syscall0(syscall_num)                    \
  ({                                             \
    u64 ret;                                     \
    asm volatile(                                \
        "mov x8, %1\n"                           \
        "svc 0\n"                                \
        "mov %0, x0\n"                           \
        : "=r"(ret)                              \
        : "r"((u64)syscall_num)                  \
        : "x0", "x8", "memory");                 \
    ret;                                         \
  })

#define syscall1(syscall_num, arg1)              \
  ({                                             \
    u64 ret;                                     \
    asm volatile(                                \
        "mov x8, %1\n"                           \
        "mov x0, %2\n"                           \
        "svc 0\n"                                \
        "mov %0, x0\n"                           \
        : "=r"(ret)                              \
        : "r"((u64)syscall_num), "r"((u64)arg1)  \
        : "x0", "x8", "memory");                 \
    ret;                                         \
  })

#define syscall2(syscall_num, arg1, arg2)        \
  ({                                             \
    u64 ret;                                     \
    asm volatile(                                \
        "mov x8, %1\n"                           \
        "mov x0, %2\n"                           \
        "mov x1, %3\n"                           \
        "svc 0\n"                                \
        "mov %0, x0\n"                           \
        : "=r"(ret)                              \
        : "r"((u64)syscall_num), "r"((u64)arg1), "r"((u64)arg2) \
        : "x0", "x1", "x8", "memory");           \
    ret;                                         \
  })

#define syscall3(syscall_num, arg1, arg2, arg3)  \
  ({                                             \
    u64 ret;                                     \
    asm volatile(                                \
        "mov x8, %1\n"                           \
        "mov x0, %2\n"                           \
        "mov x1, %3\n"                           \
        "mov x2, %4\n"                           \
        "svc 0\n"                                \
        "mov %0, x0\n"                           \
        : "=r"(ret)                              \
        : "r"((u64)syscall_num), "r"((u64)arg1), "r"((u64)arg2), "r"((u64)arg3) \
        : "x0", "x1", "x2", "x8", "memory");     \
    ret;                                         \
  })

#define syscall4(syscall_num, arg1, arg2, arg3, arg4)  \
  ({                                             \
    u64 ret;                                     \
    asm volatile(                                \
        "mov x8, %1\n"                           \
        "mov x0, %2\n"                           \
        "mov x1, %3\n"                           \
        "mov x2, %4\n"                           \
        "mov x3, %5\n"                           \
        "svc 0\n"                                \
        "mov %0, x0\n"                           \
        : "=r"(ret)                              \
        : "r"((u64)syscall_num), "r"((u64)arg1), "r"((u64)arg2), "r"((u64)arg3), "r"((u64)arg4) \
        : "x0", "x1", "x2", "x3", "x8", "memory"); \
    ret;                                         \
  })

// CPU functions
u32 cpu_get_id(void);
int cpu_get_number(void);
void cpu_init(void);
void cpu_halt(void);
void cpu_wait(void);
u64 cpu_get_cs(void);
int cpu_tas(volatile int* addr, int newval);
void cpu_set_page(u64 page_table);
void cpu_invalid_tlb(void);
void cp15_invalidate_icache(void);
void cpu_enable_page(void);
u64 cpu_get_fault(void);
u64 cpu_read_ttbr0(void);

#endif
