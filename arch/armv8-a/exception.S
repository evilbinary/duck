.section ".text"
.global exception_vectors

// ARM64 Exception Vector Table
// Each vector is 128 bytes (32 instructions)
// Layout:
// 0x000 - 0x07F: Current EL with SP0
// 0x080 - 0x0FF: Current EL with SPx
// 0x100 - 0x17F: Lower EL using AArch64
// 0x180 - 0x1FF: Lower EL using AArch32
//
// Each section has 4 entries:
// - 0x00: Synchronous exception
// - 0x20: IRQ
// - 0x40: FIQ
// - 0x60: SError

.align 11
exception_vectors:
// Current EL with SP0 (shouldn't happen)
    b exception_sp0_sync     // 0x00: Synchronous
    nop
    nop
    nop
    nop
    nop
    nop
    b exception_sp0_irq      // 0x20: IRQ
    nop
    nop
    nop
    nop
    nop
    nop
    b exception_sp0_fiq      // 0x40: FIQ
    nop
    nop
    nop
    nop
    nop
    nop
    b exception_sp0_serror   // 0x60: SError
    nop
    nop
    nop
    nop
    nop
    nop

// Current EL with SPx (kernel exceptions)
    b exception_current_sync // 0x80: Synchronous
    nop
    nop
    nop
    nop
    nop
    nop
    b exception_current_irq  // 0xA0: IRQ
    nop
    nop
    nop
    nop
    nop
    nop
    b exception_current_fiq  // 0xC0: FIQ
    nop
    nop
    nop
    nop
    nop
    nop
    b exception_current_serror // 0xE0: SError
    nop
    nop
    nop
    nop
    nop
    nop

// Lower EL using AArch64 (user mode exceptions)
    b exception_lower_sync   // 0x100: Synchronous
    nop
    nop
    nop
    nop
    nop
    nop
    b exception_lower_irq    // 0x120: IRQ
    nop
    nop
    nop
    nop
    nop
    nop
    b exception_lower_fiq    // 0x140: FIQ
    nop
    nop
    nop
    nop
    nop
    nop
    b exception_lower_serror // 0x160: SError
    nop
    nop
    nop
    nop
    nop
    nop

// Lower EL using AArch32
    b exception_lower_sync   // 0x180: Synchronous (reuse AArch64 handler)
    nop
    nop
    nop
    nop
    nop
    nop
    b exception_lower_irq    // 0x1A0: IRQ
    nop
    nop
    nop
    nop
    nop
    nop
    b exception_lower_fiq    // 0x1C0: FIQ
    nop
    nop
    nop
    nop
    nop
    nop
    b exception_lower_serror // 0x1E0: SError

// ========================================
// SP0 exceptions (should not happen)
// ========================================
exception_sp0_sync:
exception_sp0_irq:
exception_sp0_fiq:
exception_sp0_serror:
    b .

// ========================================
// Current EL exceptions (kernel mode)
// ========================================
.macro save_context
    // Save all general purpose registers (order matches interrupt_context_t)
    // Stack grows down: save x0-x29 first (at lower addresses)
    // then psr,pc, finally no,code at sp (top of struct)
    stp     x0, x1, [sp, #-16]!
    stp     x2, x3, [sp, #-16]!
    stp     x4, x5, [sp, #-16]!
    stp     x6, x7, [sp, #-16]!
    stp     x8, x9, [sp, #-16]!
    stp     x10, x11, [sp, #-16]!
    stp     x12, x13, [sp, #-16]!
    stp     x14, x15, [sp, #-16]!
    stp     x16, x17, [sp, #-16]!
    stp     x18, x19, [sp, #-16]!
    stp     x20, x21, [sp, #-16]!
    stp     x22, x23, [sp, #-16]!
    stp     x24, x25, [sp, #-16]!
    stp     x26, x27, [sp, #-16]!
    stp     x28, x29, [sp, #-16]!
    
    // Save lr and sp_el0 (user stack pointer)
    mrs     x0, sp_el0
    stp     lr, x0, [sp, #-16]!
    
    // Save PSTATE and exception return address
    mrs     x0, spsr_el1
    mrs     x1, elr_el1
    stp     x0, x1, [sp, #-16]!
    
    // Reserve space for no and code (at top of struct, sp points here)
    sub     sp, sp, #16
.endm

.macro restore_context
    // Skip no and code
    add     sp, sp, #16
    
    // Restore PSTATE and return address
    ldp     x0, x1, [sp], #16
    msr     spsr_el1, x0
    msr     elr_el1, x1
    
    // Restore lr and sp_el0 (user stack pointer)
    ldp     lr, x0, [sp], #16
    msr     sp_el0, x0
    
    // Restore general purpose registers
    ldp     x28, x29, [sp], #16
    ldp     x26, x27, [sp], #16
    ldp     x24, x25, [sp], #16
    ldp     x22, x23, [sp], #16
    ldp     x20, x21, [sp], #16
    ldp     x18, x19, [sp], #16
    ldp     x16, x17, [sp], #16
    ldp     x14, x15, [sp], #16
    ldp     x12, x13, [sp], #16
    ldp     x10, x11, [sp], #16
    ldp     x8, x9, [sp], #16
    ldp     x6, x7, [sp], #16
    ldp     x4, x5, [sp], #16
    ldp     x2, x3, [sp], #16
    ldp     x0, x1, [sp], #16
    
    eret
.endm

// Current EL synchronous exception
exception_current_sync:
    save_context

    // Set exception number - use EX_DATA_FAULT as base, sync_handler will refine
    mov     x0, #2              // EX_DATA_FAULT
    str     x0, [sp]
    mov     x1, #0
    str     x1, [sp, #8]        // code = 0

    // Call C handler
    mov     x0, sp
    bl      sync_handler

    restore_context

// Current EL IRQ
exception_current_irq:
    save_context

    mov     x0, #4              // EX_IRQ
    str     x0, [sp]
    mov     x1, #0
    str     x1, [sp, #8]

    mov     x0, sp
    bl      irq_handler

    restore_context

// Current EL FIQ
exception_current_fiq:
    save_context

    mov     x0, #6              // EX_OTHER (for FIQ)
    str     x0, [sp]
    mov     x1, #0
    str     x1, [sp, #8]

    mov     x0, sp
    bl      fiq_handler

    restore_context

// Current EL SError
exception_current_serror:
    save_context

    mov     x0, #6              // EX_OTHER (for SError)
    str     x0, [sp]
    mov     x1, #0
    str     x1, [sp, #8]

    mov     x0, sp
    bl      serror_handler

    restore_context

// ========================================
// Lower EL exceptions (user mode)
// ========================================

// Lower EL synchronous exception
exception_lower_sync:
    save_context

    mov     x0, #3              // EX_SYS_CALL (for syscall from user)
    str     x0, [sp]

    // Get exception class from ESR
    mrs     x1, esr_el1
    lsr     x1, x1, #26         // Extract EC field
    str     x1, [sp, #8]

    mov     x0, sp
    bl      sync_handler

    restore_context

// Lower EL IRQ
exception_lower_irq:
    save_context

    mov     x0, #4              // EX_IRQ
    str     x0, [sp]
    mov     x1, #0
    str     x1, [sp, #8]

    mov     x0, sp
    bl      irq_handler

    restore_context

// Lower EL FIQ
exception_lower_fiq:
    save_context

    mov     x0, #6              // EX_OTHER
    str     x0, [sp]
    mov     x1, #0
    str     x1, [sp, #8]

    mov     x0, sp
    bl      fiq_handler

    restore_context

// Lower EL SError
exception_lower_serror:
    save_context

    mov     x0, #6              // EX_OTHER
    str     x0, [sp]
    mov     x1, #0
    str     x1, [sp, #8]

    mov     x0, sp
    bl      serror_handler

    restore_context
