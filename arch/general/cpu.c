/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "cpu.h"

#include <signal.h>

#include "context.h"

extern boot_info_t* boot_info;
u32 cpus_id[MAX_CPU];

void** genral_syscall_table = NULL;

void general_sys_fn_init(void** syscall_table) {
  genral_syscall_table = syscall_table;
}

void cpu_init() {
  for (int i = 0; i < MAX_CPU; i++) {
    cpus_id[i] = i;
  }
}

void cpu_halt() { usleep(1000 * 1000); }

ulong cpu_get_cs(void) {
  ulong result;

  return result;
}

int cpu_tas(volatile int* addr, int newval) {
  int result = newval;

  return result;
}

void cpu_set_page(u32 page_table) {}

void cpu_backtrace(void) {}

u32 cpu_get_id() { return 0; }

void cpu_wait() { usleep(1000 * 1000); }

int cpu_get_number() { return boot_info->tss_number; }

u32 cpu_get_index(int idx) {
  if (idx < 0 || idx > cpu_get_number()) {
    kprintf("out of bound get cpu idx\n");
    return 0;
  }
  return cpus_id[idx];
}

int cpu_init_id(u32 id) { return 0; }

int cpu_start_id(u32 id, u32 entry) { return 0; }

void cpu_delay(int n) {
  for (int i = 0; i < 10000 * n; i++)
    ;
}

u32 cpu_get_fault() { return 0; }

void cpu_enable_page() {}

#define printf

void check() {
  context_t* current = thread_current_context();
  pthread_t p = pthread_self();
  while (true) {
    if (p == *((pthread_t*)current->thread)) {
      pthread_kill(current->thread, SIGCONT);
      break;
    } else {
      pthread_kill(current->thread, SIGSTOP);
    }
    usleep(1000 * 10);
    current = thread_current_context();
  }
}

void* syscall0(u32 num) {
  int ret;
  check();

  if (genral_syscall_table == NULL) {
    sys_fn_init_regist(general_sys_fn_init);
  }

  int (*fn)() = genral_syscall_table[num];

  if (fn != NULL) {
    printf("syscall num %d fn %x\n", num, fn);
    ret = fn();
    printf("syscall num %d fn %x ret %x\n", num, fn, ret);
  }

  return ret;
}

void* syscall1(u32 num, void* arg0) {
  int ret;

  check();

  if (genral_syscall_table == NULL) {
    sys_fn_init_regist(general_sys_fn_init);
  }

  int (*fn)(void* arg0) = genral_syscall_table[num];

  if (fn != NULL) {
    printf("syscall num %d fn %x\n", num, fn);
    ret = fn(arg0);
    printf("syscall num %d fn %x ret %x\n", num, fn, ret);
  }

  return ret;
}
void* syscall2(u32 num, void* arg0, void* arg1) {
  int ret;

  check();

  if (genral_syscall_table == NULL) {
    sys_fn_init_regist(general_sys_fn_init);
  }

  int (*fn)(void* arg0, void* arg1) = genral_syscall_table[num];

  if (fn != NULL) {
    printf("syscall num %d fn %x\n", num, fn);
    ret = fn(arg0, arg1);
    printf("syscall num %d fn %x ret %x\n", num, fn, ret);
  }

  return ret;
}
void* syscall3(u32 num, void* arg0, void* arg1, void* arg2) {
  u32 ret = 0;

  check();

  if (genral_syscall_table == NULL) {
    sys_fn_init_regist(general_sys_fn_init);
  }

  int (*fn)(void* arg0, void* arg1, void* arg2) = genral_syscall_table[num];

  if (fn != NULL) {
    printf("syscall num %d fn %x\n", num, fn);
    ret = fn(arg0, arg1, arg2);
    printf("syscall num %d fn %x ret %x\n", num, fn, ret);
  }

  return ret;
}

void* syscall4(u32 num, void* arg0, void* arg1, void* arg2, void* arg3) {
  u32 ret = 0;
  usleep(1000);
  check();

  if (genral_syscall_table == NULL) {
    sys_fn_init_regist(general_sys_fn_init);
  }

  int (*fn)(void* arg0, void* arg1, void* arg2, void* arg3) =
      genral_syscall_table[num];

  if (fn != NULL) {
    printf("syscall num %d fn %x\n", num, fn);
    ret = fn(arg0, arg1, arg2, arg3);
    printf("syscall num %d fn %x ret %x\n", num, fn, ret);
  }

  return ret;
}

void* syscall5(u32 num, void* arg0, void* arg1, void* arg2, void* arg3,
               void* arg4) {
  u32 ret = 0;

  check();

  if (genral_syscall_table == NULL) {
    sys_fn_init_regist(general_sys_fn_init);
  }

  int (*fn)(void* arg0, void* arg1, void* arg2, void* arg3, void* arg4) =
      genral_syscall_table[num];

  if (fn != NULL) {
    printf("syscall num %d fn %x\n", num, fn);
    ret = fn(arg0, arg1, arg2, arg3, arg4);
    printf("syscall num %d fn %x ret %x\n", num, fn, ret);
  }

  return ret;
}