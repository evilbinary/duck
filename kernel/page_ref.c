/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "page_ref.h"

#include "kernel/memory.h"

static u16* g_page_ref;
static u32 g_page_ref_n;

/* arch 提供的"释放页"hook（各 arch mm.c 定义）；kernel 注册实现 */
extern int (*page_put_page)(void* pa);

void page_ref_init(void) {
#ifdef CONFIG_COW
  /* 物理内存总量由架构层报告（arch/pmemory.c），不写死 */
  extern ullong mm_get_total(void);
  u64 total = (u64)mm_get_total();
  if (total == 0) {
    log_error("page_ref: total memory is 0\n");
    return;
  }
  g_page_ref_n = (u32)(total >> 12);
  if (g_page_ref_n > (1u << 20)) { /* 上限 4GB */
    g_page_ref_n = 1u << 20;
  }
  g_page_ref = kmalloc((size_t)g_page_ref_n * sizeof(u16), DEFAULT_TYPE);
  if (g_page_ref == NULL) {
    g_page_ref_n = 0;
    log_error("page_ref: table alloc failed\n");
    return;
  }
  kmemset(g_page_ref, 0, (size_t)g_page_ref_n * sizeof(u16));
  /* 把释放策略交给 arch 的 page_destroy：ref 归零才放 */
  page_put_page = page_put;
  log_info("page_ref: table %u pages\n", g_page_ref_n);
#endif
}

static inline int page_ref_idx(u32 pa) {
  u32 i = pa >> 12;
  if (g_page_ref == NULL || i >= g_page_ref_n) {
    return -1;
  }
  return (int)i;
}

u32 page_ref_get(u32 pa) {
  int i = page_ref_idx(pa);
  return i < 0 ? 1u : g_page_ref[i];
}

void page_ref_set(u32 pa, u32 v) {
  int i = page_ref_idx(pa);
  if (i >= 0) {
    g_page_ref[i] = (u16)v;
  }
}

void page_ref_inc(u32 pa) {
  int i = page_ref_idx(pa);
  if (i >= 0 && g_page_ref[i] < 0xffff) {
    g_page_ref[i]++;
  }
}

u32 page_ref_dec(u32 pa) {
  int i = page_ref_idx(pa);
  if (i < 0) {
    return 1u;
  }
  if (g_page_ref[i] > 0) {
    g_page_ref[i]--;
  }
  return g_page_ref[i];
}

int page_put(void* pa) {
  return page_ref_dec((u32)(uintptr_t)pa) == 0 ? 0 : 1;
}
