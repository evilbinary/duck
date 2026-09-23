/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "arch/cpu.h"
#include "arch/display.h"
#include "arch/pmemory.h"
#include "cpu.h"

#define PAGE_DIR_NUMBER 4096 * 4

extern boot_info_t* boot_info;

void page_map_on(page_dir_t* l1, u32 virtualaddr, u32 physaddr, u32 flags) {}

void* page_v2p(void* page, void* vaddr) {
  void* phyaddr = NULL;

  return vaddr;
}

u32* page_create(u32 level) {
  u32* page_dir_ptr_tab =
      mm_alloc_zero_align(sizeof(u32) * PAGE_DIR_NUMBER, PAGE_SIZE);
  return page_dir_ptr_tab;
}

/* TODO(page_destroy): 本架构尚未实现用户页表归还（暂不释放，等同
 * 705d987 之前的泄漏行为）。实现时参照 armv8-a/armv7-a 的 page_destroy。 */
void page_destroy(u32* upage) { (void)upage; }


void page_copy(u32* old_page, u32* new_page) {
  // kprintf("page_clone:%x %x\n",old_page,new_page);
  if (old_page == NULL) {
    kprintf("page clone error old page null\n");
    return;
  }
}

u32* page_clone(u32* old_page_dir, u32 level) {
  u32* page_dir_ptr_tab = page_create(level);
  page_copy(old_page_dir, page_dir_ptr_tab);
  return page_dir_ptr_tab;
}

void page_unmap_on(page_dir_t* page, u32 virtualaddr) {}

void mm_page_enable(u32 page_dir) {
  kprintf("enable page\n");
  cpu_enable_page();
  kprintf("paging success\n");
}

void mm_init_default(u32 kernel_page_dir) {}
#ifdef CONFIG_COW
/* ---- COW 接口：本架构暂未实现，给空实现保证 kernel 侧可链接 ----
 * （armv5 已有真实实现，其余平台按需补齐） */
int page_fault_is_write(void) { return 0; }
int page_cow_query(u32* upage, u32 va, u32* pa) { (void)upage; (void)va; (void)pa; return 0; }
void page_cow_apply(u32* upage, u32 va, u32 new_pa) { (void)upage; (void)va; (void)new_pa; }
#endif
