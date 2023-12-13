/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "arch/cpu.h"
#include "arch/display.h"
#include "arch/pmemory.h"
#include "cpu.h"

#define PAGE_SHIFT 12
#define PTE_PPN_SHIFT 10
#define PTE_PPN(pte) ((pte) >> PTE_PPN_SHIFT)

typedef unsigned long pte_t;
#define PAGE_DIR_NUMBER 512

u32* page_create(u32 level) {
  u32* page_dir_ptr_tab =
      mm_alloc_zero_align(sizeof(u32) * PAGE_DIR_NUMBER, PAGE_SIZE);
  return page_dir_ptr_tab;
}

void page_copy(u32* old_page, u32* new_page) {}

u32* page_clone(u32* old_page_dir, u32 level) {
  u32* page_dir_ptr_tab = page_create(level);
  page_copy(old_page_dir, page_dir_ptr_tab);
  return page_dir_ptr_tab;
}

void page_map_on(page_dir_t* l1, u32 virtualaddr, u32 physaddr, u32 flags) {
  // Sv32 方式 10+10+12
  u32 l1_index = (virtualaddr >> 22) & 0x3ff;
  u32 l2_index = (virtualaddr >> 12) & 0x3ff;
  u32 l3_index = virtualaddr & 0x3ff;

  u32* l2 = ((u32)l1[l1_index]) & ~0xfff;
  if (l2 == NULL) {
    l2 = mm_alloc_zero_align(PAGE_DIR_NUMBER * sizeof(u32), PAGE_SIZE);
    l1[l1_index] = (((u32)l2)) | PTE_V |flags ;
  }
  l2[l2_index] = ((physaddr >> 12) << 12) | PTE_V | flags;

}

void page_unmap_on(page_dir_t* page, u32 virtualaddr) {}


void* page_v2p(void* page, void* vaddr) {
  if (page == NULL) {
    kprintf("page v2p page is null\n");
    return vaddr;
  }
  void* phyaddr = NULL;
  u32* l1 = page;
  u32 l1_index = (u32)vaddr >> 22;
  u32 l2_index = (u32)vaddr >> 12 & 0xFF;
  u32 offset = (u32)vaddr & 0x0FFF;

  u32* l2 = ((u32)l1[l1_index]) & 0xFFFFFC00;
  if (l2 == NULL) {
    return NULL;
  }
  phyaddr = (l2[l2_index] >> 12) << 12;
  if (phyaddr == NULL) {
    return NULL;
  }
  // kprintf("page_v2p vaddr %x paddr %x\n",vaddr,phyaddr);
  return phyaddr + offset;

}

void mm_init_default(u32 kernel_page_dir) {}

void mm_page_enable(u32 page_dir) {
  cpu_set_page(page_dir);
  // start_dump();
  kprintf("enable page\n");
  cpu_enable_page();
  kprintf("paging success\n");
}
