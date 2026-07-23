/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "arch/cpu.h"
#include "arch/display.h"
#include "arch/pmemory.h"
#include "cpu.h"
#include "gpio.h"

#define PAGE_DIR_NUMBER 1024

extern boot_info_t* boot_info;

u32* page_create(u32 level) {
  u32* page_dir_ptr_tab =
      mm_alloc_zero_align(sizeof(u32) * PAGE_DIR_NUMBER, PAGE_SIZE);
  return page_dir_ptr_tab;
}

void page_map_on(page_dir_t* l1, u32 virtualaddr, u32 physaddr, u32 flags) {
  u32 l1_index = virtualaddr >> 20;
  u32 l2_index = (virtualaddr >> 12) & 0xFF;
  u32* l2 = (u32*)(l1[l1_index] & 0xFFFFFC00);
  if (l2 == NULL) {
    l2 = mm_alloc_zero_align(PAGE_SIZE, PAGE_SIZE);
    if (l2 == NULL) {
      kprintf("lx6 page_map_on alloc l2 failed\n");
      return;
    }
    kmemset(l2, 0, PAGE_SIZE);
    l1[l1_index] = ((u32)l2 & 0xFFFFFC00) | PAGE_P;
  }
  l2[l2_index] = ((physaddr >> 12) << 12) | PAGE_P | flags;
}

void* page_v2p(void* page, void* vaddr) {
  void* phyaddr = NULL;
  if (page == NULL) {
    return vaddr;
  }
  u32* l1 = page;
  u32 l1_index = (u32)vaddr >> 20;
  u32 l2_index = (u32)vaddr >> 12 & 0xFF;
  u32 offset = (u32)vaddr & 0x0FFF;
  u32 l2_addr = l1[l1_index] & 0xFFFFFC00;
  if (l2_addr == 0) {
    return NULL;
  }
  u32* l2 = (u32*)l2_addr;
  phyaddr = (void*)((l2[l2_index] >> 12) << 12);
  if (phyaddr == NULL) {
    return NULL;
  }
  return phyaddr + offset;
}

void mm_page_enable(u32 page_dir) {
  cpu_set_page(page_dir);
  // start_dump();
  kprintf("enable page\n");
  cpu_enable_page();
  kprintf("paging success\n");
}

void page_copy(u32* old_page, u32* new_page) {
  if (old_page == NULL || new_page == NULL) {
    return;
  }
  for (int l1_index = 0; l1_index < PAGE_DIR_NUMBER; l1_index++) {
    u32* l2 = (u32*)(old_page[l1_index] & 0xFFFFFC00);
    if (l2 != NULL) {
      u32* new_l2 = mm_alloc_zero_align(PAGE_SIZE, PAGE_SIZE);
      if (new_l2 == NULL) {
        kprintf("lx6 page_clone alloc l2 failed\n");
        return;
      }
      kmemmove(new_l2, l2, PAGE_SIZE);
      new_page[l1_index] = ((u32)new_l2 & 0xFFFFFC00) | (old_page[l1_index] & 0x3FF);
    }
  }
}

u32* page_clone(u32* old_page_dir, u32 level) {
  u32* page_dir_ptr_tab = page_create(level);
  page_copy(old_page_dir, page_dir_ptr_tab);
  return page_dir_ptr_tab;
}

void page_unmap_on(page_dir_t* page, u32 virtualaddr) {
  u32* l1 = page;
  u32 l1_index = virtualaddr >> 20;
  u32 l2_index = virtualaddr >> 12 & 0xFF;
  u32* l2 = ((u32)l1[l1_index]) & 0xFFFFFC00;
  if (l2 != NULL) {
    // l1[l1_index] = 0;
    l2[l2_index] = 0;
  }
}

void mm_init_default() { kprintf("lx6 mm init default\n"); }
