/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/

#include "../interrupt.h"
#include "interrupt.h"
#include "context.h"
#include "cpu.h"
#include "kernel/io.h"

extern boot_info_t* boot_info;

interrupt_handler_t interrutp_handlers[IDT_NUMBER];


// ============================================================
// Exception vector table - 2KB aligned, entries at 0x80 steps
// ============================================================
__attribute__((naked, aligned(2048), section(".text.vectors")))
void exception_vectors(void) {
  __asm__ volatile(
    // Group 0: Current EL with SP_EL0 (EL1t uses SP_EL0)
    // Route to the same handlers as "current EL with SP_ELx" so EL1t threads
    // can use SVC/IRQ without falling into the old halt stubs.
    "b exception_sp0_sync\n"
    ".align 7\n"
    "b exception_sp0_irq\n"
    ".align 7\n"
    "b exception_sp0_fiq\n"
    ".align 7\n"
    "b exception_sp0_serror\n"     // 0x180: SError（原 b . 死循环 ⇒ 黑屏元凶之一）

    // Group 1: Current EL with SP_ELx (kernel exceptions)
    ".align 7\n"
    "b exception_current_sync\n"   // 0x200: kernel sync (SVC / data abort)
    ".align 7\n"
    "b exception_current_irq\n"    // 0x280: kernel IRQ
    ".align 7\n"
    "b exception_current_fiq\n"    // 0x300: kernel FIQ
    ".align 7\n"
    "b exception_current_serror\n" // 0x380: kernel SError（原 b . 死循环 = v41/v36 黑屏元凶）

    // Group 2: Lower EL AArch64 (user → EL1)
    ".align 7\n"
    "b exception_lower_sync\n"     // 0x400: user sync (SVC / page fault)
    ".align 7\n"
    "b exception_lower_irq\n"      // 0x480: user IRQ
    ".align 7\n"
    "b exception_lower_fiq\n"      // 0x500: user FIQ
    ".align 7\n"
    "b exception_lower_serror\n"   // 0x580: user SError（原 b . 死循环）

    // Group 3: Lower EL AArch32 (unsupported)
    ".align 7\n"
    "b .\n"
    ".align 7\n"
    "b .\n"
    ".align 7\n"
    "b .\n"
    ".align 7\n"
    "b .\n"
  );
}

// ============================================================
// Current EL with SP_EL0 (EL1t): switch to SP_EL1 stack first
// ============================================================
INTERRUPT_SERVICE
void exception_sp0_sync(void) {
  __asm__ volatile(
      "msr spsel, #1\n"
      "b exception_current_sync\n");
}

INTERRUPT_SERVICE
void exception_sp0_irq(void) {
  __asm__ volatile(
      "msr spsel, #1\n"
      "b exception_current_irq\n");
}

INTERRUPT_SERVICE
void exception_sp0_fiq(void) {
  __asm__ volatile(
      "msr spsel, #1\n"
      "b exception_current_fiq\n");
}

// ============================================================
// Current EL synchronous (kernel SVC / kernel data abort)
// Kernel threads can call schedule() via SVC (e.g. thread_yield),
// which may invoke context_switch.  Use interrupt_exit_ret() so
// SP_EL1 is reset to ic base from x0 before popping, same as IRQ.
// ============================================================
INTERRUPT_SERVICE
void exception_current_sync(void) {
  interrupt_entering_code(EX_SYS_CALL, 0, 0);
  interrupt_process(sync_handler);
  interrupt_exit_ret();
}

// ============================================================
// Current EL IRQ - may cause context switch → use exit_ret
// ============================================================
INTERRUPT_SERVICE
void exception_current_irq(void) {
  interrupt_entering_code(EX_IRQ, 0, 0);
  interrupt_process(interrupt_default_handler);
  /* 【切勿在此加任何函数调用】x0 = handler 返回值 = 待恢复的 ic 指针；
   * 任何 bl 都会踩掉 x0 ⇒ eret 到垃圾地址、静默卡死（实测教训）。 */
  interrupt_exit_ret();
}

// ============================================================
// Current EL FIQ
// ============================================================
INTERRUPT_SERVICE
void exception_current_fiq(void) {
  interrupt_entering_code(EX_OTHER, 0, 0);
  interrupt_process(interrupt_default_handler);
  interrupt_exit();
}

// ============================================================
// Lower EL synchronous (user SVC / user page fault)
// Use interrupt_exit() like armv7-a: restore from current sp directly,
// ignoring the return value. exception_process() already syncs ic to
// current->ctx->ksp, and sys_exec updates ksp then copies back to ic,
// so the stack context is correct.
// ============================================================
INTERRUPT_SERVICE
void exception_lower_sync(void) {
  interrupt_entering_code(EX_SYS_CALL, 0, 0);
  interrupt_process(sync_handler);
  interrupt_exit();
}

// ============================================================
// Lower EL IRQ - may context switch → use exit_ret
// ============================================================
INTERRUPT_SERVICE
void exception_lower_irq(void) {
  interrupt_entering_code(EX_IRQ, 0, 0);
  interrupt_process(interrupt_default_handler);
  /* 【切勿在此加任何函数调用】x0 = handler 返回值 = 待恢复的 ic 指针；
   * 任何 bl 都会踩掉 x0 ⇒ eret 到垃圾地址、静默卡死（实测教训）。 */
  interrupt_exit_ret();
}

// ============================================================
// Lower EL FIQ
// ============================================================
INTERRUPT_SERVICE
void exception_lower_fiq(void) {
  interrupt_entering_code(EX_OTHER, 0, 0);
  interrupt_process(interrupt_default_handler);
  interrupt_exit();
}

// ============================================================
// SError slots (v43): 原来是 "b ." 死循环 —— boot/MMU 期一旦积累 pending
// SError（imprecise 异步错误），任何 A=0 的 eret 落地即焊死在 0x380，
// 黑屏且零输出（v36/v41 的真凶）。现在走 probe 打印 ESR/ISR 后正常 eret，
// 既消费 pending SError 又把来源暴露出来。
// ============================================================
INTERRUPT_SERVICE
void exception_sp0_serror(void) {
  __asm__ volatile(
      "msr spsel, #1\n"
      "b exception_current_serror");
}

INTERRUPT_SERVICE
void exception_current_serror(void) {
  interrupt_entering_code(EX_OTHER, 0, 0);
  interrupt_process(serror_probe);
  interrupt_exit_ret();
}

INTERRUPT_SERVICE
void exception_lower_serror(void) {
  interrupt_entering_code(EX_OTHER, 0, 0);
  interrupt_process(serror_probe);
  interrupt_exit();
}

// ============================================================
// C-level synchronous exception dispatch.
//
// Design mirrors armv7-a: just set ic->no to the right exception
// number and call interrupt_default_handler.  No ksp_end tricks.
// interrupt_exit / interrupt_exit_ret restore from sp directly
// (same thread) or from x0 (switched thread).
// ============================================================
void* sync_handler(interrupt_context_t* ic) {
  u64 esr = read_esr();
  u32 ec  = get_exception_class(esr);
  u64 far = read_far();

  ic->code = ec;

  switch (ec) {
    case ESR_ELx_EC_SVC64:
      ic->no = EX_SYS_CALL;
      break;
    case ESR_ELx_EC_UNKNOWN:
      // When user threads run at EL1t during bring-up, some libc syscall
      // sites can arrive here with EC=0 even though x8 still carries a valid
      // Linux syscall number. Handle that path as a syscall instead of halting.
      if ((ic->x8 <= 518) || (ic->x8 >= 0x5000 && ic->x8 <= 0x5017)) {
        ic->no = EX_SYS_CALL;
        break;
      }
      kprintf("sync: unknown ec=%x esr=%lx pc=%lx sp=%lx x8=%lx\n", ec, esr,
              ic->pc, ic->sp, ic->x8);
      context_dump_interrupt(ic);
      ic->no = EX_UNDEF;
      break;
    case ESR_ELx_EC_DABT_LOW:
    case ESR_ELx_EC_DABT_CUR:
      ic->no = EX_DATA_FAULT;
      break;
    case ESR_ELx_EC_IABT_LOW:
    case ESR_ELx_EC_IABT_CUR:
      ic->no = EX_PREF_ABORT;
      break;
    default:
      kprintf("sync: unknown ec=%x esr=%lx pc=%lx sp=%lx x8=%lx\n", ec, esr,
              ic->pc, ic->sp, ic->x8);
      context_dump_interrupt(ic);
      ic->no = EX_UNDEF;
      break;
  }

  interrupt_default_handler(ic);
  // Always return the ic pointer.  sys_exec sets ic->x0 = args and ic->pc = entry,
  // so the return value is already in the right place for eret.
  return ic;
}

// ============================================================
// C support functions
// ============================================================

/* display.c 的直写串口输出（绕过 io 通道/日志锁）—— bring-up 标记用 */
extern void puts(char* text);
extern void puthex(unsigned long v);

/* SError 探针（v43 转正）：boot 期存在一个尚未定位的周期性异步错误源（每轮开机
 * 积累一次 pending SError，esr=0xbe000411），保留此低频 [SR] 打印用于未来诊断；
 * 返回 ic 正常 eret —— 消费掉 pending SError。 */
void* serror_probe(interrupt_context_t* ic) {
  u64 esr, isr;
  int cpu = cpu_get_id();
  asm volatile("mrs %0, esr_el1" : "=r"(esr));
  asm volatile("mrs %0, isr_el1" : "=r"(isr));
  puts("[SR] c");
  puthex(cpu);
  puts(" esr=");
  puthex((unsigned long)esr);
  puts(" isr=");
  puthex((unsigned long)isr);
  puts(" pc=");
  puthex((unsigned long)ic->pc);
  puts("\n");
  return ic;
}

void interrupt_init(int cpu) {
  kprintf("interrupt init cpu %d\n", cpu);

  asm volatile("msr vbar_el1, %0" : : "r"((u64)exception_vectors) : "memory");
  isb();

  if (cpu_get_id() == 0) {
    for (int i = 0; i < IDT_NUMBER; i++) {
      interrutp_handlers[i] = NULL;
    }
    boot_info->idt_base = (void*)exception_vectors; /* 若 boot_info 是野指针 ⇒ 这里 abort */
    boot_info->idt_number = IDT_NUMBER;
  }
}

void interrupt_regist(u32 vec, interrupt_handler_t handler) {
  interrutp_handlers[vec] = handler;
}

void exception_info(interrupt_context_t* ic) {
  static const char* msgs[] = {
    "", "RESET", "DATA FAULT", "SYS CALL",
    "IRQ",  "UNDEF", "OTHER", "PREF ABORT", "PERMISSION"
  };
  int cpu = cpu_get_id();
  /* 【直写串口报错】早期 fault 时 kprintf 本身可能有问题（打印锁/缓冲/栈），
   * 所以先用 puts/puthex 直写（与 display_init 同一可靠通路），保证 fault 的
   * 类型、ESR/FAR/ELR 一定出得来 —— 这是"静默死"变"可诊断"的关键。 */
  puts("[EXC] cpu=");
  puthex((unsigned long)cpu);
  puts(" no=");
  puthex(ic->no);
  puts(" esr=");
  puthex(read_esr());
  puts(" far=");
  puthex(read_far());
  puts(" elr=");
  puthex(ic->pc);
  puts(" psr=");
  puthex(ic->psr);
  puts("\n");
  if (ic->no < sizeof(msgs) / sizeof(msgs[0])) {
    kprintf("exception cpu %d no %d: %s\n", cpu, ic->no, msgs[ic->no]);
  } else {
    kprintf("exception cpu %d no %lld\n", cpu, ic->no);
  }
  kprintf("esr: %lx far: %lx\n", read_esr(), read_far());
  context_dump_interrupt(ic);
}

void interrupt_regist_all(void) {
  // handlers registered via interrupt_regist()
}