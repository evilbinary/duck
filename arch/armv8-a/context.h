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
// Stack layout matches interrupt_entering_code save order
typedef struct interrupt_context {
  // At sp+0, sp+8 (pushed last in save_context)
  u64 no;
  u64 code;
  
  // At sp+16, sp+24 (saved from SPSR_EL1, ELR_EL1)
  u64 psr;        // PSTATE (from SPSR_EL1) at sp+16
  u64 pc;         // Exception return address (from ELR_EL1) at sp+24
  
  // At sp+32, sp+40 (lr and sp_el0)
  u64 lr;         // Link register (X30) at sp+32
  u64 sp;         // User stack pointer (SP_EL0) at sp+40
  
  // At sp+48 onwards (general registers, pushed first)
  u64 x28;        // sp+48
  u64 x29;        // sp+56
  u64 x26;        // sp+64
  u64 x27;        // sp+72
  u64 x24;        // sp+80
  u64 x25;        // sp+88
  u64 x22;        // sp+96
  u64 x23;        // sp+104
  u64 x20;        // sp+112
  u64 x21;        // sp+120
  u64 x18;        // sp+128
  u64 x19;        // sp+136
  u64 x16;        // sp+144
  u64 x17;        // sp+152
  u64 x14;        // sp+160
  u64 x15;        // sp+168
  u64 x12;        // sp+176
  u64 x13;        // sp+184
  u64 x10;        // sp+192
  u64 x11;        // sp+200
  u64 x8;         // sp+208
  u64 x9;         // sp+216
  u64 x6;         // sp+224
  u64 x7;         // sp+232
  u64 x4;         // sp+240
  u64 x5;         // sp+248
  u64 x2;         // sp+256
  u64 x3;         // sp+264
  u64 x0;         // sp+272
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
// Note: naked functions don't need clobber list as compiler doesn't manage registers
#define interrupt_process(X) \
  asm volatile(              \
      "bl " #X              \
      "\n"                   \
      :                      \
      :                      \
      : "memory")

// Save context on exception entry (matches interrupt_context_t layout)
// Stack layout after this macro (matches struct order):
//   [sp+0]:   no
//   [sp+8]:   code
//   [sp+16]:  psr
//   [sp+24]:  pc
//   [sp+32]:  lr
//   [sp+40]:  sp_el0
//   [sp+48]:  x28
//   ...
//   [sp+272]: x0
//   [sp+280]: x1
// Note: TYPE parameter is kept for compatibility with armv7-a but not used in armv8
#define interrupt_entering_code(VEC, CODE, TYPE) \
  asm volatile(                                  \
      /* Save all registers first (they get overwritten below) */ \
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
      "mov x3, %0\n" \
      "mov x4, %1\n" \
      "stp x3, x4, [sp, #-16]!\n" \
      /* Now sp points to interrupt_context_t */ \
      "mov x0, sp\n" \
      :                                          \
      : "i"(VEC), "i"(CODE) \
      : "memory")

#define interrupt_exit_context(ksp) \
  asm volatile(                              \
      "mov sp, %0 \n"                         \
      /* Skip no, code */ \
      "add sp, sp, #16\n" \
      /* Load psr, pc */ \
      "ldp x0, x1, [sp], #16\n" \
      "msr spsr_el1, x0\n" \
      "msr elr_el1, x1\n" \
      /* Load lr, sp_el0 */ \
      "ldp lr, x0, [sp], #16\n" \
      "msr sp_el0, x0\n" \
      /* Load x28-x29, ..., x0-x1 */ \
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
      /* Skip no, code */ \
      "add sp, sp, #16\n" \
      /* Load psr, pc */ \
      "ldp x0, x1, [sp], #16\n" \
      "msr spsr_el1, x0\n" \
      "msr elr_el1, x1\n" \
      /* Load lr, sp_el0 */ \
      "ldp lr, x0, [sp], #16\n" \
      "msr sp_el0, x0\n" \
      /* Load x28-x29, ..., x0-x1 */ \
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

// interrupt_exit_ret: restore context from x0 (ic pointer returned by handler).
// ic->no (offset 0) holds next thread's ksp_end (set by context_switch).
// We use x30 as scratch (safe: overwritten by lr restore), x9 as temp for msr.
#define interrupt_exit_ret()          \
  asm volatile(                       \
      /* x30 = ic base */             \
      "mov x30, x0\n"                 \
      /* sp = next->ksp_end (stashed in ic->no at offset 0) */ \
      "ldr x9, [x30, #0]\n"          \
      "mov sp,  x9\n"                 \
      /* psr -> SPSR_EL1 */           \
      "ldr x9,  [x30, #16]\n"        \
      "msr spsr_el1, x9\n"           \
      /* pc -> ELR_EL1 */             \
      "ldr x9,  [x30, #24]\n"        \
      "msr elr_el1, x9\n"            \
      /* lr */                        \
      "ldr x9,  [x30, #32]\n"        \
      "mov lr,   x9\n"                \
      /* sp_el0 */                    \
      "ldr x9,  [x30, #40]\n"        \
      "msr sp_el0, x9\n"             \
      "ldr x28, [x30, #48]\n"        \
      "ldr x29, [x30, #56]\n"        \
      "ldr x26, [x30, #64]\n"        \
      "ldr x27, [x30, #72]\n"        \
      "ldr x24, [x30, #80]\n"        \
      "ldr x25, [x30, #88]\n"        \
      "ldr x22, [x30, #96]\n"        \
      "ldr x23, [x30, #104]\n"       \
      "ldr x20, [x30, #112]\n"       \
      "ldr x21, [x30, #120]\n"       \
      "ldr x18, [x30, #128]\n"       \
      "ldr x19, [x30, #136]\n"       \
      "ldr x16, [x30, #144]\n"       \
      "ldr x17, [x30, #152]\n"       \
      "ldr x14, [x30, #160]\n"       \
      "ldr x15, [x30, #168]\n"       \
      "ldr x12, [x30, #176]\n"       \
      "ldr x13, [x30, #184]\n"       \
      "ldr x10, [x30, #192]\n"       \
      "ldr x11, [x30, #200]\n"       \
      "ldr x8,  [x30, #208]\n"       \
      "ldr x6,  [x30, #224]\n"       \
      "ldr x7,  [x30, #232]\n"       \
      "ldr x4,  [x30, #240]\n"       \
      "ldr x5,  [x30, #248]\n"       \
      "ldr x2,  [x30, #256]\n"       \
      "ldr x3,  [x30, #264]\n"       \
      "ldr x9,  [x30, #216]\n"       \
      "ldr x1,  [x30, #280]\n"       \
      "ldr x0,  [x30, #272]\n"       \
      "eret\n"                        \
      :                               \
      :                               \
      : "memory")

#define context_switch_page(ctx, page_dir) cpu_set_page((u64)(page_dir))

#define context_fn(context) context->x8
#define context_ret(context) context->x0
#define context_set_entry(context, entry) (((interrupt_context_t*)context)->pc = entry);

// For first thread start: restore full context and eret into the thread.
// interrupt_context_t offsets (all u64, 8 bytes each):
//   +0:  no   +8:  code  +16: psr  +24: pc   +32: lr   +40: sp_el0
//   +48: x28  +56: x29   +64: x26  +72: x27  +80: x24  +88: x25
//   +96: x22  +104:x23   +112:x20  +120:x21  +128:x18  +136:x19
//   +144:x16  +152:x17   +160:x14  +168:x15  +176:x12  +184:x13
//   +192:x10  +200:x11   +208:x8   +216:x9   +224:x6   +232:x7
//   +240:x4   +248:x5    +256:x2   +264:x3   +272:x0   +280:x1
// %0 = ic ptr, %1 = ksp_end (kernel stack top for SP_EL1)
// context_start: restore full CPU state from interrupt_context_t and eret.
// ic offsets: +0 no, +8 code, +16 psr, +24 pc, +32 lr, +40 sp_el0,
//             +48 x28, +56 x29, ... +272 x0, +280 x1
//
// Pin _ic to x30 and _ksp to x9 via specific-register constraints.
// This avoids the "impossible constraints" error from having too many
// clobbers while still keeping both values safe across the sp switch.
#define context_start(duck_context)                              \
  do {                                                           \
    register u64 _ic  asm("x30") = (u64)(duck_context->ksp);   \
    register u64 _ksp asm("x9")  = (u64)(duck_context->ksp_end);\
    asm volatile(                                                \
        /* switch SP_EL1 to thread's kernel stack top.          \
           _ic (x30) and _ksp (x9) already in registers —       \
           no stack access needed after this point. */           \
        "mov sp,  x9\n"                                          \
        "ldr x9,  [x30, #16]\n"   /* psr      */                \
        "msr spsr_el1, x9\n"                                     \
        "ldr x9,  [x30, #24]\n"   /* pc       */                \
        "msr elr_el1, x9\n"                                      \
        "ldr x9,  [x30, #32]\n"   /* lr       */                \
        "mov lr,   x9\n"                                         \
        "ldr x9,  [x30, #40]\n"   /* sp_el0   */                \
        "msr sp_el0, x9\n"                                       \
        "ldr x28, [x30, #48]\n"                                  \
        "ldr x29, [x30, #56]\n"                                  \
        "ldr x26, [x30, #64]\n"                                  \
        "ldr x27, [x30, #72]\n"                                  \
        "ldr x24, [x30, #80]\n"                                  \
        "ldr x25, [x30, #88]\n"                                  \
        "ldr x22, [x30, #96]\n"                                  \
        "ldr x23, [x30, #104]\n"                                 \
        "ldr x20, [x30, #112]\n"                                 \
        "ldr x21, [x30, #120]\n"                                 \
        "ldr x18, [x30, #128]\n"                                 \
        "ldr x19, [x30, #136]\n"                                 \
        "ldr x16, [x30, #144]\n"                                 \
        "ldr x17, [x30, #152]\n"                                 \
        "ldr x14, [x30, #160]\n"                                 \
        "ldr x15, [x30, #168]\n"                                 \
        "ldr x12, [x30, #176]\n"                                 \
        "ldr x13, [x30, #184]\n"                                 \
        "ldr x10, [x30, #192]\n"                                 \
        "ldr x11, [x30, #200]\n"                                 \
        "ldr x8,  [x30, #208]\n"                                 \
        "ldr x6,  [x30, #224]\n"                                 \
        "ldr x7,  [x30, #232]\n"                                 \
        "ldr x4,  [x30, #240]\n"                                 \
        "ldr x5,  [x30, #248]\n"                                 \
        "ldr x2,  [x30, #256]\n"                                 \
        "ldr x3,  [x30, #264]\n"                                 \
        "ldr x9,  [x30, #216]\n"  /* x9  (restore last) */      \
        "ldr x1,  [x30, #280]\n"                                 \
        "ldr x0,  [x30, #272]\n"                                 \
        /* lr already restored via 'mov lr, x9' above */        \
        "eret\n"                                                  \
        :                                                         \
        : "r"(_ic), "r"(_ksp)                                    \
        : "memory");                                              \
  } while (0)

#define context_restore(duck_context) interrupt_exit_context(duck_context->ksp)

int context_clone(context_t* des, context_t* src);
int context_init(context_t* context, u64 ksp_top, u64 usp_top, u64 entry,
                 u32 level, int cpu);
void context_dump(context_t* c);
void context_dump_interrupt(interrupt_context_t* ic);
void context_dump_fault(interrupt_context_t* context, u64 fault_addr);

#endif
