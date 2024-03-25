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

#define PAGE_DIR_NUMBER 1024 * 1

extern boot_info_t* boot_info;
static u32 page_dir[PAGE_DIR_NUMBER] __attribute__((aligned(0x100)));

u32* page_create(u32 level) {
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
}

u32* page_clone(u32* old_page_dir, u32 level) {
  u32* page_dir_ptr_tab = page_create(level);
  page_copy(old_page_dir, page_dir_ptr_tab);
  return page_dir_ptr_tab;
}

void page_map_on(page_dir_t* l1, u32 virtualaddr, u32 physaddr, u32 flags) {
  u32 l1_index = virtualaddr >> 20;
  u32 l2_index = virtualaddr >> 12 & 0xFF;
  u32* l2 = ((u32)l1[l1_index]) & 0xFFFFFC00;
  if (l2 == NULL) {
    l2 = mm_alloc_zero_align(0x1000, 0x1000);
    kmemset(l2, 0, 0x1000);
    l1[l1_index] = (((u32)l2) & 0xFFFFFC00) | L1_DESC;
  }
  l2[l2_index] = ((physaddr >> 12) << 12) | L2_DESC | flags;
}

void* page_v2p(void* page, void* vaddr) {
  void* phyaddr = NULL;
  u32* l1 = page;
  u32 l1_index = (u32)vaddr >> 20;
  u32 l2_index = (u32)vaddr >> 12 & 0xFF;
  u32* l2 = ((u32)l1[l1_index]) & 0xFFFFFC00;
  if (l2 != NULL) {
    // kprintf("l2 %x\n",l2);
    phyaddr = (l2[l2_index] >> 12) << 12;
  }
  // kprintf("page_v2p vaddr %x paddr %x\n",vaddr,phyaddr);
  return phyaddr;
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

void mm_page_enable(u32 page_dir) {
  // cpu_disable_page();
  // cpu_icache_disable();
  cp15_invalidate_icache();
  cpu_invalid_tlb();

  cpu_set_page(page_dir);
  // start_dump();
  kprintf("enable page\n");
  cpu_enable_page();
  kprintf("paging success\n");
}



void unpage_map_on(page_dir_t* page, u32 virtualaddr) {
  u32* l1 = page;
  u32 l1_index = virtualaddr >> 20;
  u32 l2_index = virtualaddr >> 12 & 0xFF;
  u32* l2 = ((u32)l1[l1_index]) & 0xFFFFFC00;
  if (l2 != NULL) {
    // l1[l1_index] = 0;
    l2[l2_index] = 0;
  }
}


void mm_init_default() {
  kprintf("mem init default\n");
  // mm_test();
  // boot_info->pdt_base = page_dir;
  // kmemset(page_dir, 0, 4096 * 8);

  // u32 address = 0;
  // kprintf("map %x - %x\n", address, 0x1000 * 1024 * 10);
  // for (int j = 0; j < 1024 * 10; j++) {
  //   page_map(address, address, 0);
  //   address += 0x1000;
  // }
  // address = boot_info->kernel_entry;
  // kprintf("map kernel %x ", address);
  // int i;
  // for (i = 0; i < (((u32)boot_info->kernel_size) / 0x1000 + 6); i++) {
  //   page_map(address, address, L2_TEXT_1 | L2_CB);
  //   address += 0x1000;
  // }
  // kprintf("- %x\n", address);

  // kprintf("map page end\n");

  // // cpu_disable_page();
  // // cpu_icache_disable();
  // cp15_invalidate_icache();
  // cpu_invalid_tlb();

  // cpu_set_domain(0x07070707);
  // // cpu_set_domain(0xffffffff);
  // // cpu_set_domain(0x55555555);
  // cpu_set_page(page_dir);

  // // start_dump();
  // kprintf("enable page\n");

  // cpu_enable_page();
  // kprintf("paging pae scucess\n");
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