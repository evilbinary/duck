/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "kernel/kernel.h"

#define __a_barrier_kuser 0xffff0fa0
#define __a_cas_kuser 0xffff0fc0
#define __a_gettp_kuser 0xffff0fe0
#define __a_ver 0xffff0ffc

int musl_gettp() {
  log_debug("musl gettp\n");
  return sys_thread_self();
}

int musl_init(void) {
  log_info("musl init\n");

  // make musl happy ^_^!!
  //  u32 addr = __a_barrier_kuser & ~0xfff;
  //  page_map(addr, addr, 0);

  // *((int *)__a_ver) = 2;
  // *(int *)__a_barrier_kuser = 0;
  // *(int *)__a_cas_kuser = 0;
  // *(int *)__a_gettp_kuser = musl_gettp;

#ifdef ARM
  //enable float
  asm("mrc p15, 0, r0, c1, c0, 2\n"
      "orr r0,r0,#0x300000\n"
      "orr r0,r0,#0xC00000\n"
      "mcr p15, 0, r0, c1, c0, 2\n"
      "mov r0,#0x40000000\n"
      "fmxr fpexc,r0");
#endif


      return 0;
}

void musl_exit(void) { log_info("musl exit\n"); }

module_t musl_module = {.name = "musl", .init = musl_init, .exit = musl_exit};
