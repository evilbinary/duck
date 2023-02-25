/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "arch/cpu.h"
#include "arch/display.h"
#include "arch/pmemory.h"
#include "cpu.h"
#include "gpio.h"

#define PAGE_DIR_NUMBER 4096

extern boot_info_t* boot_info;
extern memory_manager_t mmt;

u32 kernel_page_dir[PAGE_DIR_NUMBER] __attribute__((aligned(0x4000)));

u32* page_create(u32 level) {
  if (level == KERNEL_MODE) {
    // must no cache
    return kernel_page_dir;
  }
  u32* page_dir_ptr_tab =
      mm_alloc_zero_align(sizeof(u32) * PAGE_DIR_NUMBER, PAGE_SIZE * 4);
  return page_dir_ptr_tab;
}

void page_copy(u32* old_page, u32* new_page) {
  // kprintf("page_clone:%x %x\n",old_page,new_page);
  if (old_page == NULL) {
    kprintf("page clone error old page null\n");
    return;
  }
  u32* l1 = old_page;
  u32* new_l1 = new_page;
  // kprintf("page clone %x to %x\n",old_page,new_page);
  for (int l1_index = 0; l1_index < 4096; l1_index++) {
    u32* l2 = ((u32)l1[l1_index]) & 0xFFFFFC00;
    if (l2 != NULL) {
      page_dir_t* new_l2 = mm_alloc_zero_align(256 * sizeof(u32), 0x1000);
      new_l1[l1_index] = (((u32)new_l2) & 0xFFFFFC00) | L1_DESC;
      // kprintf("%d %x\n", l1_index, (u32)l2>>10 );
      for (int l2_index = 0; l2_index < 256; l2_index++) {
        u32* addr = l2[l2_index] >> 12;
        if (addr != NULL || l1_index == 0) {
          new_l2[l2_index] = l2[l2_index];
          // kprintf("  %d %x\n", l2_index, addr);
        }
      }
    }
  }
}

u32* page_clone(u32* old_page_dir, u32 level) {
  if (level == KERNEL_MODE) {
    // must no cache
    return kernel_page_dir;
  }
  if (level == USER_MODE) {
    u32* page_dir_ptr_tab =page_create(level);
    if (old_page_dir == NULL) {
      old_page_dir = kernel_page_dir;
    }
    page_copy(old_page_dir, page_dir_ptr_tab);
    return page_dir_ptr_tab;
  }
  return page_create(level);
}

void page_map_on(page_dir_t* l1, u32 virtualaddr, u32 physaddr, u32 flags) {
  // kprintf("map page %x vaddr:%x paddr:%x\n",l1,virtualaddr,physaddr);
  u32 l1_index = virtualaddr >> 20;
  u32 l2_index = virtualaddr >> 12 & 0xFF;
  u32* l2 = ((u32)l1[l1_index]) & 0xFFFFFC00;
  if (l2 == NULL) {
    l2 = mm_alloc_zero_align(256 * sizeof(u32), 0x1000);
    l1[l1_index] = (((u32)l2) & 0xFFFFFC00) | L1_DESC;
  }
  l2[l2_index] = ((physaddr >> 12) << 12) | L2_DESC | flags;
}

void page_map(u32 virtualaddr, u32 physaddr, u32 flags) {
  page_map_on(kernel_page_dir, virtualaddr, physaddr, flags);
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

void* page_v2p(void* page, void* vaddr) {
  if( page==NULL){
    kprintf("page v2p page is null\n");
  }
  void* phyaddr = NULL;
  u32* l1 = page;
  u32 l1_index = (u32)vaddr >> 20;
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

void mm_init_default() {
  mm_test();
  boot_info->pdt_base = kernel_page_dir;
  kmemset(kernel_page_dir, 0, sizeof(u32) * PAGE_DIR_NUMBER);

  // map mem block 100 page 4000k
  map_mem_block(PAGE_SIZE * 10000, 0);

  // map 0 - 0x80000
  map_range(0, 0, PAGE_SIZE * 20, 0);

  // map kernel
  // map_kernel(L2_TEXT_1 | L2_NCB, L2_NCNB);

  map_kernel(0, 0);

  kprintf("map page end\n");
}

void mm_page_enable() {
  // cpu_disable_page();
  // cpu_icache_disable();
  cp15_invalidate_icache();
  cpu_invalid_tlb();

  cpu_set_domain(0x07070707);
  // cpu_set_domain(0xffffffff);
  // cpu_set_domain(0x55555555);

  cpu_set_page(kernel_page_dir);
  // start_dump();
  kprintf("enable page\n");
  cpu_enable_page();
  kprintf("paging scucess\n");
}

void mm_test() {
  // page_map(0x90000,0x600000,3);
  // u32* addr=mm_alloc(256);
  // *addr=0x123456;
  // kprintf("===============+>\n");
  // u32 *p = 0x1c2ac0c;
  // *p = 1 << 6;
  // kprintf("p=%x\n", *p);
}


