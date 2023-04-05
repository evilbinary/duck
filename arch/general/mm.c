/*******************************************************************
 * Copyright 2021-2080 evilbinary
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

void unpage_map_on(page_dir_t* page, u32 virtualaddr) {}

void mm_page_enable(u32 page_dir) {
  kprintf("enable page\n");
  cpu_enable_page();
  kprintf("paging scucess\n");
}

void mm_init_default(u32 kernel_page_dir) {}