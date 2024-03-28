/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "kernel/kernel.h"

#define __a_barrier_kuser 0xffff0fa0
#define __a_cas_kuser 0xffff0fc0
#define __a_gettp_kuser 0xffff0fe0
#define __a_ver 0xffff0ffc

void* musl_vector = NULL;

void musl_barrier_kuser() {
  log_debug("barrier_kuser\n");
  dmb();
}

int musl_cas_kuser(int* ptr, int oldval, int newval) {
  log_debug("musl cas_kuser\n");
  return cpu_cmpxchg(ptr, oldval, newval);
}

int musl_gettp(int a, int b, int c) {
  log_debug("musl gettp\n");
  return sys_thread_self();
}

int musl_init(void) {
  log_info("musl init\n");

#ifdef ARMV7_A
  log_info("enable float\n");
  // enable float
  asm("mrc p15, 0, r0, c1, c0, 2\n"
      "orr r0,r0,#0x300000\n"
      "orr r0,r0,#0xC00000\n"
      "mcr p15, 0, r0, c1, c0, 2\n"
      "mov r0,#0x40000000\n"
      "fmxr fpexc,r0");
#else defined(ARMV5)
  // make musl happy ^_^!! User space atomic ops on ARMv5 and earlier
  // if (musl_vector == NULL) {
  //   musl_vector = kmalloc_alignment(PAGE_SIZE, PAGE_SIZE, KERNEL_TYPE);
  // }

  // u32 addr = __a_barrier_kuser & ~0xfff;
  // page_map(addr, addr, PAGE_RWX);

  // *((u32 *)__a_ver) = 2;
  // *((u32 *)__a_barrier_kuser) = &musl_barrier_kuser;
  // *((u32 *)__a_cas_kuser) = &musl_cas_kuser;
  // *((u32 *)__a_gettp_kuser) = &musl_gettp;

#endif

  return 0;
}

void musl_exit(void) { log_info("musl exit\n"); }

module_t musl_module = {.name = "musl", .init = musl_init, .exit = musl_exit};
