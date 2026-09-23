/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "kernel/kernel.h"
#include "kernel/page.h"

#ifdef ARMV5
/* ============ ARMv5 musl kuser helper 页 ============
 * ARMv5(ARM926) 无 LDREX/STREX，musl 的用户态原子操作走内核 kuser helper：
 *   __a_cas_ptr     = 0xffff0fc0
 *   __a_barrier_ptr = 0xffff0fa0
 *   __a_gettp_ptr   = 0xffff0fe0
 *   version         = *(int*)0xffff0ffc
 * 这里在 0xffff0000 放一页【用户态可执行】的自包含指令（不能跳内核函数，
 * 它运行在用户态），并按固定偏移填好。
 */
#define KUSER_BASE 0xffff0000u

static void musl_kuser_init(void) {
  u32* page = kmalloc_alignment(PAGE_SIZE, PAGE_SIZE, KERNEL_TYPE);
  if (page == NULL) {
    log_error("musl: kuser page alloc failed\n");
    return;
  }
  u32* w = page;
  for (u32 i = 0; i < PAGE_SIZE / 4; i++) {
    w[i] = 0;
  }

  /* 0x0fa0 __kuser_memory_barrier: mcr p15,0,r0,c7,c10,5 ; bx lr */
  w[0xfa0 / 4 + 0] = 0xEE070FBA;
  w[0xfa0 / 4 + 1] = 0xE12FFF1E;

  /* 0x0fc0 __kuser_cmpxchg (r0=old, r1=new, r2=ptr; 返回 0=成功):
   *   ldr r3,[r2]; subs r3,r3,r0; streq r1,[r2]; rsbs r0,r3,#0; bx lr */
  w[0xfc0 / 4 + 0] = 0xE5923000; /* ldr  r3, [r2]      */
  w[0xfc0 / 4 + 1] = 0xE0533000; /* subs r3, r3, r0    */
  w[0xfc0 / 4 + 2] = 0x05823000; /* streq r1, [r2]     */
  w[0xfc0 / 4 + 3] = 0xE2730000; /* rsbs r0, r3, #0    */
  w[0xfc0 / 4 + 4] = 0xE12FFF1E; /* bx   lr            */

  /* 0x0fe0 __kuser_get_tls: mrc p15,0,r0,c13,c0,3 ; bx lr */
  w[0xfe0 / 4 + 0] = 0xEE1D0F70;
  w[0xfe0 / 4 + 1] = 0xE12FFF1E;

  /* 0x0ffc __kuser_helper_version（musl 读 0xffff0ffc） */
  w[0xffc / 4] = 5;

  cpu_flush_dcache_range((unsigned long)page,
                         (unsigned long)page + PAGE_SIZE);

  /* 映射到内核页表；用户进程首次访问时经缺页从内核页表按需映射到 upage */
  page_map(KUSER_BASE, (u32)(uintptr_t)page, PAGE_USER);
  log_info("musl: kuser helper %x -> %x\n", KUSER_BASE,
           (u32)(uintptr_t)page);
}
#endif

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
#elif defined(ARMV5)
  musl_kuser_init();
#endif

  return 0;
}

void musl_exit(void) { log_info("musl exit\n"); }

module_t musl_module = {.name = "musl", .init = musl_init, .exit = musl_exit};
