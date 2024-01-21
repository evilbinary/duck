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
static u32 page_dir[PAGE_DIR_NUMBER] __attribute__((aligned(0x4000)));

void page_map_on(page_dir_t* l1, u32 virtualaddr, u32 physaddr, u32 flags) {
  
}

void page_map(u32 virtualaddr, u32 physaddr, u32 flags) {
  page_map_on(boot_info->pdt_base, virtualaddr, physaddr, flags);
}

void mm_init_default() {
  // init map
  kprintf("map page end\n");
 
  //init page
  kprintf("enable page\n");

  kprintf("paging pae scucess\n");
}


void mm_page_enable(u32 page_dir) {

}

void* page_v2p(void* page, void* vaddr) {
  void* phyaddr = NULL;
 
  return phyaddr;
}

void* page_create(u32 level) {

    return NULL;
}

void unpage_map_on(page_dir_t* page, u32 virtualaddr) {
  
}

