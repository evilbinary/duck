/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "arch.h"
#include "context.h"
#include "kernel/string.h"

boot_info_t* boot_info = NULL;

void context_inherit_live(context_t* child, interrupt_context_t* live) {
  if (child == NULL || live == NULL) {
    return;
  }
  interrupt_context_t* slot = child->ksp;
  if (slot == NULL) {
    return;
  }
  kmemmove(slot, live, sizeof(interrupt_context_t));
  child->ic = slot;
  child->ksp = slot;
}
extern u32 write_channel_number;

void arch_init(boot_info_t* boot, int cpu) {
  if (cpu == 0) {
    boot_info = boot;
    write_channel_number = 0;
    platform_init();

    cpu_init(cpu);
    display_init();
    interrupt_init(cpu);

    mm_init();
    platform_end();
  } else {
#ifdef MP_ENABLE
    ap_init(cpu);
#endif
  }
}