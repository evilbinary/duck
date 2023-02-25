/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "arch/mm.h"

#include "arch/cpu.h"
#include "arch/display.h"
#include "cpu.h"
#include "gpio.h"

#define PAGE_DIR_NUMBER 4096 * 4

extern boot_info_t* boot_info;
static u32 page_dir[PAGE_DIR_NUMBER] __attribute__((aligned(0x4000)));

void page_map_on(page_dir_t* l1, u32 virtualaddr, u32 physaddr, u32 flags) {
  
}

void page_map(u32 virtualaddr, u32 physaddr, u32 flags) {
  page_map_on(boot_info->pdt_base, virtualaddr, physaddr, flags);
}

void mm_init_default() {
//   mm_test();
//   boot_info->pdt_base = page_dir;
//   kmemset(page_dir, 0, 4096 * 8);

//   u32 address = 0;
//   kprintf("map %x - %x\n", address, 0x1000 * 1024 * 10);
//   for (int j = 0; j < 1024 * 10; j++) {
//     page_map(address, address, 0);
//     address += 0x1000;
//   }
//   address = boot_info->kernel_entry;
//   kprintf("map kernel %x ", address);
//   int i;
//   for (i = 0; i < (((u32)boot_info->kernel_size) / 0x1000 + 6); i++) {
//     page_map(address, address, L2_TEXT_1 | L2_CB);
//     address += 0x1000;
//   }
//   kprintf("- %x\n", address);

//   page_map(MMIO_BASE, MMIO_BASE, 0);
//   page_map(UART0_DR, UART0_DR, 0);
//   page_map(CORE0_TIMER_IRQCNTL, CORE0_TIMER_IRQCNTL, 0);
// #ifdef V3S
//   // memory
//   address = 0x40000000;
//   kprintf("map memory %x ", address);
//   for (int i = 0; i < 0x2000000 / 0x1000; i++) {
//     page_map(address, address, L2_TEXT_1 | L2_NCB);
//     address += 0x1000;
//   }
//   kprintf("- %x\n", address);

//   // ccu -pio timer
//   page_map(0x01C20000, 0x01C20000, 0);
//   // uart
//   page_map(0x01C28000, 0x01C28000, 0);
//   // timer
//   page_map(0x01C20C00, 0x01C20C00, 0);
//   // gic
//   page_map(0x01C81000, 0x01C81000, 0);
//   page_map(0x01C82000, 0x01C82000, 0);

// #endif

  kprintf("map page end\n");

 

  cpu_set_domain(0x07070707);
  // cpu_set_domain(0xffffffff);
  // cpu_set_domain(0x55555555);
  cpu_set_page(page_dir);

  // start_dump();
  kprintf("enable page\n");

  cpu_enable_page();
  kprintf("paging pae scucess\n");
}


void* page_v2p(void* page, void* vaddr) {
  void* phyaddr = NULL;
 
  return phyaddr;
}

void page_clone(u32* old_page, u32* new_page) {
  // kprintf("page_clone:%x %x\n",old_page,new_page);
  if (old_page == NULL) {
    kprintf("page clone error old page null\n");
    return;
  }
  
}

u32* page_create(u32 level) {
  if (level == KERNEL_MODE) {
    // must no cache
    return kernel_page_dir;
  }
  u32* page_dir_ptr_tab =
      mm_alloc_zero_align(sizeof(u32) * PAGE_DIR_NUMBER, PAGE_SIZE * 4);
  return page_dir_ptr_tab;
}

void unpage_map_on(page_dir_t* page, u32 virtualaddr) {
  
}

