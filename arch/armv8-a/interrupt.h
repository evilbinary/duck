/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef INTERRUPT_H
#define INTERRUPT_H

#include "arch/boot.h"
#include "libs/include/types.h"
#include "context.h"

// Exception vector numbers (ARM64 specific)
#define EX_SYNC_EL0      0    // Synchronous exception from EL0
#define EX_IRQ_EL0       1    // IRQ from EL0
#define EX_FIQ_EL0       2    // FIQ from EL0
#define EX_SERROR_EL0    3    // SError from EL0
#define EX_SYNC_EL1      4    // Synchronous exception from EL1
#define EX_IRQ_EL1       5    // IRQ from EL1
#define EX_FIQ_EL1       6    // FIQ from EL1
#define EX_SERROR_EL1    7    // SError from EL1

// ARM64 exception types (from ESR_EL1)
#define ESR_ELx_EC_SHIFT     26
#define ESR_ELx_EC_MASK      (0x3F << ESR_ELx_EC_SHIFT)

// Exception classes
#define ESR_ELx_EC_UNKNOWN   0x00
#define ESR_ELx_EC_SVC64     0x15   // SVC instruction in AArch64
#define ESR_ELx_EC_HVC64     0x16   // HVC instruction
#define ESR_ELx_EC_SMC64     0x17   // SMC instruction
#define ESR_ELx_EC_SVC32     0x11   // SVC instruction in AArch32
#define ESR_ELx_EC_IABT_LOW  0x20   // Instruction abort from lower EL
#define ESR_ELx_EC_IABT_CUR  0x21   // Instruction abort from current EL
#define ESR_ELx_EC_DABT_LOW  0x24   // Data abort from lower EL
#define ESR_ELx_EC_DABT_CUR  0x25   // Data abort from current EL
#define ESR_ELx_EC_BRK       0x3C   // Breakpoint

// IDT number for compatibility
#define IDT_NUMBER 256

// Use interrupt_handler_t from arch/interrupt.h

// Functions
void interrupt_init(void);
void interrupt_regist(u32 vec, interrupt_handler_t handler);
void interrupt_regist_all(void);
void exception_info(interrupt_context_t* ic);

// Handlers called from assembly
void sync_handler(interrupt_context_t* ic);
void irq_handler(interrupt_context_t* ic);
void fiq_handler(interrupt_context_t* ic);
void serror_handler(interrupt_context_t* ic);

// Get exception class from ESR
static inline u32 get_exception_class(u64 esr) {
  return (esr >> ESR_ELx_EC_SHIFT) & 0x3F;
}

// Get instruction syndrome from ESR
static inline u32 get_instruction_syndrome(u64 esr) {
  return esr & 0x1FFFFFF;
}

#endif
