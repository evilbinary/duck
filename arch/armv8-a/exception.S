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
.balign 128
    b exception_sp0_sync     // 0x00: Synchronous
.balign 32
    b exception_sp0_irq      // 0x20: IRQ
.balign 32
    b exception_sp0_fiq      // 0x40: FIQ
.balign 32
    b exception_sp0_serror   // 0x60: SError

// Current EL with SPx (kernel exceptions)
.balign 128
    b exception_current_sync // 0x80: Synchronous
.balign 32
    b exception_current_irq  // 0xA0: IRQ
.balign 32
    b exception_current_fiq  // 0xC0: FIQ
.balign 32
    b exception_current_serror // 0xE0: SError

// Lower EL using AArch64 (user mode exceptions)
.balign 128
    b exception_lower_sync   // 0x100: Synchronous
.balign 32
    b exception_lower_irq    // 0x120: IRQ
.balign 32
    b exception_lower_fiq    // 0x140: FIQ
.balign 32
    b exception_lower_serror // 0x160: SError

// Lower EL using AArch32
.balign 128
    b exception_lower_sync   // 0x180: Synchronous (reuse AArch64 handler)
.balign 32
    b exception_lower_irq    // 0x1A0: IRQ
.balign 32
    b exception_lower_fiq    // 0x1C0: FIQ
.balign 32
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
    // Save all general purpose registers
    stp     x29, x30, [sp, #-16]!
    stp     x27, x28, [sp, #-16]!
    stp     x25, x26, [sp, #-16]!
    stp     x23, x24, [sp, #-16]!
    stp     x21, x22, [sp, #-16]!
    stp     x19, x20, [sp, #-16]!
    stp     x17, x18, [sp, #-16]!
    stp     x15, x16, [sp, #-16]!
    stp     x13, x14, [sp, #-16]!
    stp     x11, x12, [sp, #-16]!
    stp     x9, x10, [sp, #-16]!
    stp     x7, x8, [sp, #-16]!
    stp     x5, x6, [sp, #-16]!
    stp     x3, x4, [sp, #-16]!
    stp     x1, x2, [sp, #-16]!
    stp     x0, x1, [sp, #-16]!
    
    // Save PSTATE and exception info
    mrs     x0, spsr_el1
    mrs     x1, elr_el1
    stp     x0, x1, [sp, #-16]!
    
    // Reserve space for no and code
    sub     sp, sp, #16
.endm

.macro restore_context
    // Skip no and code
    add     sp, sp, #16
    
    // Restore PSTATE and return address
    ldp     x0, x1, [sp], #16
    msr     spsr_el1, x0
    msr     elr_el1, x1
    
    // Restore general purpose registers
    ldp     x0, x1, [sp], #16
    ldp     x1, x2, [sp], #16
    ldp     x3, x4, [sp], #16
    ldp     x5, x6, [sp], #16
    ldp     x7, x8, [sp], #16
    ldp     x9, x10, [sp], #16
    ldp     x11, x12, [sp], #16
    ldp     x13, x14, [sp], #16
    ldp     x15, x16, [sp], #16
    ldp     x17, x18, [sp], #16
    ldp     x19, x20, [sp], #16
    ldp     x21, x22, [sp], #16
    ldp     x23, x24, [sp], #16
    ldp     x25, x26, [sp], #16
    ldp     x27, x28, [sp], #16
    ldp     x29, x30, [sp], #16
    
    eret
.endm

// Current EL synchronous exception
exception_current_sync:
    save_context
    
    // Set exception number
    mov     x0, #4              // EX_SYNC_EL1
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
    
    mov     x0, #5              // EX_IRQ_EL1
    str     x0, [sp]
    mov     x1, #0
    str     x1, [sp, #8]
    
    mov     x0, sp
    bl      irq_handler
    
    restore_context

// Current EL FIQ
exception_current_fiq:
    save_context
    
    mov     x0, #6              // EX_FIQ_EL1
    str     x0, [sp]
    mov     x1, #0
    str     x1, [sp, #8]
    
    mov     x0, sp
    bl      fiq_handler
    
    restore_context

// Current EL SError
exception_current_serror:
    save_context
    
    mov     x0, #7              // EX_SERROR_EL1
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
    
    mov     x0, #0              // EX_SYNC_EL0
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
    
    mov     x0, #1              // EX_IRQ_EL0
    str     x0, [sp]
    mov     x1, #0
    str     x1, [sp, #8]
    
    mov     x0, sp
    bl      irq_handler
    
    restore_context

// Lower EL FIQ
exception_lower_fiq:
    save_context
    
    mov     x0, #2              // EX_FIQ_EL0
    str     x0, [sp]
    mov     x1, #0
    str     x1, [sp, #8]
    
    mov     x0, sp
    bl      fiq_handler
    
    restore_context

// Lower EL SError
exception_lower_serror:
    save_context
    
    mov     x0, #3              // EX_SERROR_EL0
    str     x0, [sp]
    mov     x1, #0
    str     x1, [sp, #8]
    
    mov     x0, sp
    bl      serror_handler
    
    restore_context
