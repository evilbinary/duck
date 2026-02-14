#include "specreg.h"

.section .vector.text
.global _idt
.global window_overflow_4,window_underflow_4,window_overflow_8,window_underflow_8,window_overflow_12,window_underflow_12

.align 0x1000
_idt:
window_overflow_4:
/* window overflow 4 - 0x00  0*/
    s32e	a0, a5, -16
    s32e	a1, a5, -12
    s32e	a2, a5,  -8
    s32e	a3, a5,  -4
    rfwo

.align 64
window_underflow_4:
/* window underflow 4 - 0x40 1*/
    l32e	a0, a5, -16
    l32e	a1, a5, -12
    l32e	a2, a5,  -8
    l32e	a3, a5,  -4
    rfwu

.align 64
window_overflow_8:
/* windows overflow 8 - 0x80 2*/
    s32e	a0, a9, -16
    l32e	a0, a1, -12
    s32e	a1, a9, -12
    s32e	a2, a9, -8
    s32e	a3, a9, -4
    s32e	a4, a9, -32
    s32e	a5, a9, -28
    s32e	a6, a9, -24
    s32e	a7, a9, -20
    rfwo

.align 64
window_underflow_8:
/* windows underflow 8 - 0xc0 3*/
  l32e    a0, a9, -16
    l32e    a8, a9, -12  
    l32e    a2, a9, -8
    l32e    a7, a8, -12  
    l32e    a3, a9, -4
    l32e    a4, a7, -32
    l32e    a5, a7, -28
    l32e    a6, a7, -24
    l32e    a7, a7, -20
    rfwu

.align 64
window_overflow_12:
/* windows overflow 12 - 0x100 4*/
    s32e	a0, a13, -16
    l32e	a0, a1, -12
    s32e	a1, a13, -12
    s32e	a2, a13, -8
    s32e	a3, a13, -4
    s32e	a4, a13, -48
    s32e	a5, a13, -44
    s32e	a6, a13, -40
    s32e	a7, a13, -36
    s32e	a8, a13, -32
    s32e	a9, a13, -28
    s32e	a10, a13, -24
    s32e	a11, a13, -20
    rfwo

.align 64
window_underflow_12:
/* windows underflow 12 - 0x140 5*/
    l32e	a0, a13, -16
    l32e	a1, a13, -12
    l32e	a2, a13, -8
    l32e	a11, a1, -12
    l32e	a3, a13, -4
    l32e	a4, a11, -48
    l32e	a5, a11, -44
    l32e	a6, a11, -40
    l32e	a7, a11, -36
    l32e	a8, a11, -32
    l32e	a9, a11, -28
    l32e	a10, a11, -24
    l32e	a11, a11, -20
    rfwu

.align 64
/* interrupt level 2 - 0x180  6*/
    rsr.excsave2 a0
    call0 l2_handler
    rfi 2

.align 64
/* interrupt level 3 - 0x1c0 7*/
    rsr.excsave3 a0
    call0 l3_handler
    rfi 3

.align 64
/* interrupt level 4 - 0x200 8*/
    rsr.excsave4 a0
    call0 l4_handler

    rfi 4

.align 64
/* interrupt level 5 - 0x240 9*/
    rsr.excsave5 a0
    call0 l5_handler

    rfi 5

.align 64
/* interrupt level 6 (debug) - 0x280 10*/
    rsr.excsave6 a0
    call0 debug_excetpion_handler
    rfi 6

.align 64
/* interrupt level 7 (nmi) - 0x2c0  11*/
    rsr.excsave7 a0
    call0 nmi_excetpion_handler

    rfi 7

.type  kernel_exception,@function //12
.align 64
 kernel_exception:
    wsr.excsave1 a1
    wsr.epc1 a0
    
    call0 kernel_excetpion_handler

    rfe
.size kernel_exception, . - kernel_exception


.type  user_exception,@function //13
.align 64
user_exception:
    wsr.excsave1 a1
    wsr.epc1 a0

    call0 user_excetpion_handler

    rfe


.size user_exception, . - user_exception

.align 64
/* double exception - 0x380 */
    wsr.excsave1 a1
    wsr.depc a0
    call0 double_excetpion_handler
    rfde