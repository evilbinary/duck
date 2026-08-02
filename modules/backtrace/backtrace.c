/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "backtrace.h"

static u32 bt_dumping = 0;

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
    kprintf("backtrace: skip reentrant fault dump\n");
    return;
  }
  bt_dumping = 1;
  bt_dump(t, ic, fault_addr);
  bt_dumping = 0;
}

void bt_dump(thread_t* t, interrupt_context_t* ic, u64 fault_addr) {
  bt_frame_t frames[BT_MAX_FRAMES];
  int cpu = cpu_get_id();
  int n = bt_unwind(t, ic, frames, BT_MAX_FRAMES);

  char sym[BT_SYM_NAME_MAX];
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

int backtrace_init(void) {
  fault_hook_regist(bt_on_fault);
  bt_kernel_preload();
  kprintf("backtrace module init\n");
  return 0;
}

void backtrace_exit(void) { kprintf("backtrace module exit\n"); }

module_t backtrace_module = {
    .name = "backtrace",
    .init = backtrace_init,
    .exit = backtrace_exit,
};
