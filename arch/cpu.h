/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef CPU_H
#define CPU_H

#include "boot.h"
#include "libs/include/types.h"
#include "interrupt.h"

#ifdef ARM
  #ifdef ARMV7_A
    #include "armv7-a/cpu.h"
  #elif defined(ARMV7)
    #include "armv7/cpu.h"
  #elif defined(ARMV5)
    #include "armv5/cpu.h"
  #elif defined(ARMV8_A)
    #include "armv8-a/cpu.h"
  #else
    #error "no support arm"
  #endif
#elif defined(ARM64)
  #if defined(ARMV8_A)
    #include "armv8-a/cpu.h"
  #else
    #error "ARM64 defined but no specific architecture (e.g., ARMV8_A)"
  #endif
#elif defined(X86)
  #ifdef X86_DUCK
    #include "x86-duck/cpu.h"
  #else
    #include "x86/cpu.h"
  #endif
#elif defined(LX6)
  #include "lx6/cpu.h"
#elif defined(GENERAL)
#include "general/cpu.h"
#elif defined(RISCV)
#include "riscv/cpu.h"
#elif defined(DUMMY)
#include "dummy/cpu.h"
#else
    #error "no support"
#endif


/* ---- 统一的 cache 维护接口（跨架构） --------------------------------------
 * 共享代码（加载器 / 块设备 / DMA / 显示）只调用下面三个名字，不允许再出现
 * cp15_*、cache_inv_range、mcr p15 这类架构私有细节。各架构在自己的 cpu.c 里
 * 给出强实现；无该需求或硬件自动一致的架构由 arch.c 的弱符号空实现兜底。
 *
 * 语义约定（关键：先分清"谁写、谁读"）：
 *   cpu_flush_dcache_range(s, e)      clean + invalidate。CPU 写的数据 → 让硬件/DRAM 看到。
 *                                     用于：块设备写、DMA 源缓冲、帧缓冲交给显示控制器前。
 *   cpu_invalidate_dcache_range(s, e) 仅 invalidate。硬件写的数据 → 让 CPU 重新读。
 *                                     用于：DMA 目标缓冲、控制器回填的描述符。
 *                                     ⚠ 前提：该范围内不得有 CPU 脏数据，否则会被直接丢弃。
 *   cpu_invalidate_icache()           失效 I-cache，让取指看到最新指令。
 *                                     用于：装载/生成代码之后、进入用户态之前。
 *
 * 范围由实现负责按 cache line 向外取整，调用方不必自己对齐。 */
void cpu_flush_dcache_range(unsigned long start, unsigned long stop);
void cpu_invalidate_dcache_range(unsigned long start, unsigned long stop);
void cpu_invalidate_icache(void);

/* ---- 统一的 TLS / 线程指针接口（跨架构） ---------------------------------
 * 把"写硬件线程指针寄存器"这件架构私事从共享代码里拿掉：共享代码只调
 * cpu_set_tls()，各架构在自己的 cpu.c 里给强实现，没有该寄存器的架构用
 * arch.c 的弱符号空实现兜底（同上面三个 cache 接口）。
 *   * ARMv6K / ARMv7+ ：写 TPIDRURO(c13,c0,3)；
 *   * ARMv5(ARM926EJ-S) / ARMv7-M(cortex-m4) / x86 / riscv ：无此 CP15
 *     寄存器 → 空实现，线程指针由 current->user_tp + sys_thread_self()
 *     维护（ARMv5 上执行那条 mcr 会直接 UNDEF，实测踩过这个坑）。
 * 注意：内核当前不在上下文切换时保存/恢复该寄存器，本仓库 musl 的
 * __get_tp() 也已改用 __syscall(SYS_THREAD_SELF)；该接口主要用于兼容
 * 仍按硬件 TP 取 TLS 的用户态。 */
void cpu_set_tls(void* tp);

/* ---- 缓存效果自测（跨架构诊断接口） --------------------------------------
 * 打印 SCTLR 里的 MMU/D-cache/WB/I-cache 位，并用可用的时基比对
 * "小缓冲(可常驻 cache) vs 大缓冲(远超 cache)" 的读带宽，用来判断缓存
 * 是否真的生效。armv5(ARM926) 没有 PMU，用调度 tick 计时；armv7-a 有
 * 自己的 cpu_mem_bw_test（PMU 周期）。无实现的架构由 arch.c 弱符号兜底。
 * 只在 -DMM_DEBUG_PROBE=1 时由 kernel.c 调用。 */
void cpu_cache_selftest(void);

typedef uint32_t phys_address_t;
typedef uint32_t virtual_address_t;

typedef struct stack_frame {
  struct stack_frame* prev;
  void* return_addr;
} __attribute__((packed)) stack_frame_t;

void cpu_init(int cpu);
void cpu_halt();
void cpu_wait(void);

#define KERNEL_MODE 0
#define USER_MODE 3
#define GET_CPL(x) (((x)&0x03))  //0-3

int cpu_tas(volatile int* addr, int newval);

/* 通用 SP 读写：perf/backtrace 切换大栈用，覆盖所有架构 */
static inline u32 cpu_get_sp(void) {
#if defined(X86)
  u32 sp;
  __asm__ volatile("mov %%esp, %0" : "=r"(sp));
  return sp;
#elif defined(RISCV)
  u32 sp;
  __asm__ volatile("mv %0, sp" : "=r"(sp));
  return sp;
#elif defined(LX6)
  u32 sp;
  __asm__ volatile("mov %0, sp;" : "=r"(sp));
  return sp;
#elif defined(ARM) || defined(ARM64)
  u32 sp;
  __asm__ volatile("mov %0, sp" : "=r"(sp));
  return sp;
#else /* GENERAL / DUMMY */
  return 0;
#endif
}

static inline void cpu_set_sp(u32 sp) {
#if defined(X86)
  __asm__ volatile("mov %0, %%esp" ::"r"(sp));
#elif defined(RISCV)
  __asm__ volatile("mv sp, %0" ::"r"(sp));
#elif defined(LX6)
  __asm__ volatile("mov sp, %0;" ::"r"(sp));
#elif defined(ARM) || defined(ARM64)
  __asm__ volatile("mov sp, %0" ::"r"(sp));
#else
  (void)sp;
#endif
}

#endif