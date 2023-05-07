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

extern boot_info_t* boot_info;

u32* page_create(u32 level) { return NULL; }

void page_map_on(page_dir_t* l1, u32 virtualaddr, u32 physaddr, u32 flags) {
  // u32 l1_index = virtualaddr >> 20;
  // u32 l2_index = virtualaddr >> 12 & 0xFF;
  // u32* l2 = ((u32)l1[l1_index]) & 0xFFFFFC00;
  // if (l2 == NULL) {
  //   l2 = mm_alloc_zero_align(0x1000, 0x1000);
  //   kmemset(l2, 0, 0x1000);
  //   l1[l1_index] = (((u32)l2) & 0xFFFFFC00) | L1_DESC;
  // }
  // l2[l2_index] = ((physaddr >> 12) << 12 )|L2_DESC| flags;
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

void mm_page_enable(u32 page_dir) {
  cpu_set_page(page_dir);
  // start_dump();
  kprintf("enable page\n");
  cpu_enable_page();
  kprintf("paging scucess\n");
}

void page_clone(u32* old_page, u32* new_page) {
  // kprintf("page_clone:%x %x\n",old_page,new_page);
  // if (old_page == NULL) {
  //   kprintf("page clone error old page null\n");
  //   return;
  // }
  // u32* l1 = old_page;
  // u32* new_l1 = new_page;
  // // kprintf("page clone %x to %x\n",old_page,new_page);
  // for (int l1_index = 0; l1_index < 4096; l1_index++) {
  //   u32* l2 = ((u32)l1[l1_index]) & 0xFFFFFC00;
  //   if (l2 != NULL) {
  //     page_dir_t* new_l2 = mm_alloc_zero_align(256*sizeof(u32), 0x1000);
  //     new_l1[l1_index] = (((u32)new_l2) & 0xFFFFFC00) | L1_DESC;
  //     // kprintf("%d %x\n", l1_index, (u32)l2>>10 );
  //     for (int l2_index = 0; l2_index < 256; l2_index++) {
  //       u32* addr = l2[l2_index] >> 12;
  //       if (addr != NULL || l1_index == 0) {
  //         new_l2[l2_index] = l2[l2_index];
  //         // kprintf("  %d %x\n", l2_index, addr);
  //       }
  //     }
  //   }
  // }
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

void mm_init_default() { kprintf("mm_init_default\n"); }
