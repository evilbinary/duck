/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "backtrace.h"
#include "kernel/memory.h"

static u32 bt_dumping = 0;

static inline u32 bt_get_sp(void) {
  u32 sp;
  __asm__ volatile("mov %0, sp" : "=r"(sp));
  return sp;
}

/* 符号化预留栈预算：符号化深链至少需要这么多栈 */
#define BT_RUN_STACK_SIZE (64 * 1024)

/* name 可能未初始化（如早期内核线程），打印前校验指针落在可读
 * 映射区（低物理区 0x4000000 起、内核/用户虚拟区至 0xa0000000，
 * 覆盖内核堆、用户代码与栈），避免 kprintf %s 再次 fault */
int bt_name_valid(thread_t* t, const char* name) {
  u32 a = (u32)name;
  if (a < 0x4000000 || a >= 0xa0000000) {
    return 0;
  }
  return 1;
}

static void bt_on_fault(thread_t* t, interrupt_context_t* ic,
                        u64 fault_addr) {
  if (bt_dumping) {
    return;
  }
  bt_dumping = 1;
  /* 若当前 sp 落在可扩栈 vma 内（用户/内核线程栈），先通用扩栈到
   * 预算再跑，避免 4KB 固定栈溢出；否则走专用静态栈兜底 */
  if (t != NULL && t->vm != NULL &&
      memory_stack_ensure(t->vm, bt_get_sp(), BT_RUN_STACK_SIZE) == 0) {
    bt_dump(t, ic, fault_addr);
    bt_dumping = 0;
    return;
  }
  bt_dump(t, ic, fault_addr);
  bt_dumping = 0;
}

static void bt_dump_real(thread_t* t, interrupt_context_t* ic, u64 fault_addr) {
  static bt_frame_t frames[BT_MAX_FRAMES];
  static char sym[BT_SYM_NAME_MAX];
  int cpu = cpu_get_id();
  int n = bt_unwind(t, ic, frames, BT_MAX_FRAMES);

  bt_sym_lookup(NULL, (u32)bt_dump, 0, sym, sizeof(sym));
  kprintf("== fault backtrace cpu %d tid %d pid %d name %s no %d code %x addr %x (self %s) ==\n",
          cpu, t != NULL ? t->id : -1, t != NULL ? t->pid : -1,
          t != NULL && bt_name_valid(t, t->name) ? t->name : "?",
          ic != NULL ? ic->no : -1, ic != NULL ? ic->code : 0,
          (u32)fault_addr, sym);

  for (int i = 0; i < n; i++) {
    bt_sym_lookup(t, frames[i].addr, frames[i].mode, sym, sizeof(sym));
    kprintf("  [%d] %08x %s (%s)\n", i, frames[i].addr, sym,
            frames[i].mode == 3 ? "user" : "kernel");
  }
  if (n == 0) {
    kprintf("  (no unwind frame)\n");
  }
  kprintf("== end backtrace ==\n");
}

/* 符号化专用栈（静态，无 kmalloc/无锁）：fault 深链在 4KB 内核/异常栈上
 * 必然溢出（踩坏 LR -> UND 死循环），这里进入符号化前把真实 SP 切到这块
 * 通用大栈，跑完还原。任何平台只需 get/set sp 两个内联。 */
static u8 bt_run_stack[BT_RUN_STACK_SIZE] __attribute__((aligned(16)));

static void bt_dump_inner(thread_t* t, interrupt_context_t* ic, u64 fault_addr) {
  bt_dump_real(t, ic, fault_addr);
}

void bt_dump(thread_t* t, interrupt_context_t* ic, u64 fault_addr) {
  u32 cur = bt_get_sp();
  u32 lo = (u32)bt_run_stack;
  u32 hi = lo + (u32)BT_RUN_STACK_SIZE;
  if (cur >= lo && cur <= hi) {
    bt_dump_real(t, ic, fault_addr);
    return;
  }
  u32 saved = cur;
  __asm__ volatile("mov sp, %0" ::"r"(hi));
  bt_dump_inner(t, ic, fault_addr);
  __asm__ volatile("mov sp, %0" ::"r"(saved));
}

int backtrace_init(void) {
  fault_hook_regist(bt_on_fault);
  kprintf("backtrace module init\n");
  return 0;
}

void backtrace_exit(void) { kprintf("backtrace module exit\n"); }

module_t backtrace_module = {
    .name = "backtrace",
    .init = backtrace_init,
    .exit = backtrace_exit,
};
