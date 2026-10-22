/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * 从 sysfn.c 拆出：杂项/系统信息（print/dumps/meminfo/uname/umask）。
 ********************************************************************/
#include "sysfn.h"

#include "kernel/error.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "kernel/thread.h"
#include "kernel/version.h"

#define log_debug

/* ===================== 输出/调试 ===================== */

int sys_print(char* s) {
  thread_t* current = thread_current();
  // kprintf("sys print %d %s\n", current->id, s);
  kprintf("%s", s);
  return 0;
}

void sys_test() {
  thread_t* current = thread_current();
  kprintf("sys test %d\n", current->id);

  kprintf("-------dump thread %d-------------\n", current->id);
  thread_dump(current, DUMP_DEFAULT | DUMP_CONTEXT);
}

void sys_dumps() {
  thread_t* current = thread_current();
  thread_dumps();
}

int sys_print_at(char* s, u32 x, u32 y) {
  kprintf("%s", s);
  return 0;
}

/* ===================== 系统信息 ===================== */

int sys_mem_info() {
  memory_t* mem = memory_info();
  kprintf("total       %6dk\n", mem->total / 1024);
  kprintf("free        %6dk\n", mem->free / 1024);
  kprintf("kernel used %6dk\n", mem->kernel_used / 1024);
  kprintf("user   used %6dk\n", mem->user_used / 1024);
}

int sys_info(sysinfo_t* info) {
  memory_t* mem = memory_info();
  info->totalram = mem->total;
  info->freeram = mem->free;
  info->procs = thread_count();
  return 0;
}

/* utsname 各字段长度（Linux __NEW_UTS_LEN+1，musl 同布局）。
 * release 走 kernel/version.h 的 KERNEL_VERSION_STRING（单一来源）；
 * machine/nodename 由构建系统注入的 ARM64/RASPI3 等宏推导。 */
#define UTS_FIELD_LEN 65
static void uts_set(char* dst, const char* src) {
  kstrncpy(dst, src, UTS_FIELD_LEN - 1);
  dst[UTS_FIELD_LEN - 1] = '\0';
}

int sys_uname(char* buf) {
  if (buf == NULL) {
    return -1;
  }
  uts_set(buf + 0 * UTS_FIELD_LEN, "Linux"); /* musl/LTP 假定 Linux ABI */
  uts_set(buf + 1 * UTS_FIELD_LEN, UTS_NODENAME);
  uts_set(buf + 2 * UTS_FIELD_LEN, KERNEL_VERSION_STRING);
  uts_set(buf + 3 * UTS_FIELD_LEN, UTS_VERSION);
  uts_set(buf + 4 * UTS_FIELD_LEN, UTS_MACHINE);
  uts_set(buf + 5 * UTS_FIELD_LEN, "");
  return 0;
}

/* ===================== 其它 ===================== */

int sys_umask(int mask) {
  thread_t* current = thread_current();
  if (current == NULL) {
    log_error("current is null\n");
    return -1;
  }
  // todo

  return mask;
}

int sys_self(void* t) {
  if (t == NULL) return -2;
  thread_t* current = thread_current();
  if (current == NULL) return -1;
  kmemcpy(t, current, sizeof(thread_t));
  return 1;
}

int sys_rt_sigprocmask(int h, void* set, void* old_set) {
  return 0;
}

int sys_rt_sigaction(int signum, const struct sigaction* restrict act,
                     struct sigaction* restrict oldact) {
  log_debug("sys sigaction not impl\n");
  return 0;
}
