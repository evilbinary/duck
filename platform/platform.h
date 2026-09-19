/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef RASPI2

#define ISR_TIMER 0x0f
#define ISR_KEYBOARD 0x21
#define ISR_SYSCALL 2
#define ISR_MOUSE 0x2c
#define ISR_USB 0x09
#define MP_ENABLE 1  //多cpu

#define IDT_NUMBER 512
#define EXCEPTION_NUMBER 512

#elif defined(X86)

#define ISR_TIMER 0x20
#define ISR_KEYBOARD 0x21
#ifdef LIBC_POSIX
#define ISR_SYSCALL 0x80
#else
#define ISR_SYSCALL 0x30
#endif
#define ISR_MOUSE 0x2c

// #define MP_ENABLE 1  //多cpu

#define IDT_NUMBER 2048
#define EXCEPTION_NUMBER 2048

#elif defined(STM32F4XX)
#define ISR_TIMER 50
#define ISR_KEYBOARD 0x21
#define ISR_SYSCALL 11
#define ISR_MOUSE 0x2c

#define IDT_NUMBER 256
#define EXCEPTION_NUMBER 32

#elif defined(V3S)
#define ISR_TIMER 50
#define ISR_KEYBOARD 0x21
#define ISR_SYSCALL 2
#define ISR_MOUSE 0x2c

#define IDT_NUMBER 256
#define EXCEPTION_NUMBER 32

#elif defined(MIYOO)
#define ISR_TIMER 27
#define ISR_KEYBOARD 0x21
#define ISR_MOUSE 0x2c

#define IDT_NUMBER 256
#define EXCEPTION_NUMBER 32

#define MP_ENABLE 1  //多cpu

#elif defined(VERSATILEPB)
#define ISR_TIMER 27
#define ISR_KEYBOARD (3)
#define ISR_MOUSE (4)

#define IDT_NUMBER 256
#define EXCEPTION_NUMBER 32

#elif defined(T113_S3)
#define ISR_TIMER 91
#define ISR_KEYBOARD 0x21
#define ISR_MOUSE 0x2c

#define IDT_NUMBER 256
#define EXCEPTION_NUMBER 32

// #define MP_ENABLE 1  //多cpu

#elif defined(RASPI3) || defined(RASPI5) || defined(ARMV8_A)

#define ISR_TIMER 5
#define ISR_KEYBOARD 0x21
#define ISR_MOUSE 0x2c
#define ISR_USB 0x09

#define IDT_NUMBER 256
#define EXCEPTION_NUMBER 32

#define MP_ENABLE 1

#else

#define ISR_TIMER 5
#define ISR_KEYBOARD 0x21
#define ISR_MOUSE 0x2c
#define ISR_USB 0x09

#define IDT_NUMBER 256
#define EXCEPTION_NUMBER 32

#define MP_ENABLE 1  //多cpu

#endif

/* 【打印缓冲槽位数】libs/libkernelcommon/io.c 用它决定 printf_buffer 的槽数：
 *   多核（MP_ENABLE）：每颗 CPU 一个槽 ⇒ 并发打印时各写各的，不会互相踩坏；
 *   单核：只要 1 个槽 ⇒ 省下 (槽数-1) × KPRINT_BUF(2KB) 的 .bss。
 * 【为什么必须放在这里】io.c 是通用层、看不到 MP_ENABLE；它显式 include 本头文件
 * 来取这个策略（见 io.c 顶部）。槽数取 4 = MAX_CPU（见 arch/boot.h），io.c 里对
 * 越界的 CPU 号会回落到 0 号槽，所以这里给少了也不会踩内存。 */
#ifdef MP_ENABLE
#define KPRINT_BUF_SLOTS 4
#else
#define KPRINT_BUF_SLOTS 1
#endif

#ifndef ISR_USB
#define ISR_USB 0x09
#endif

void platform_init(void);
void platform_end(void);
void platform_map(void);
void ipi_enable(int cpu);
void ipi_send(int cpu, int vec);
void ipi_clear(int cpu);

#endif