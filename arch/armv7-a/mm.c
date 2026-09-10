/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "arch/cpu.h"
#include "arch/display.h"
#include "arch/pmemory.h"
#include "cpu.h"
#include "kernel/memory.h"

#define PAGE_DIR_NUMBER 4096

extern boot_info_t* boot_info;
extern memory_manager_t mmt;
extern void dccmvac(unsigned long mva);
extern void* page_kernel_dir(void);

u32* page_create(u32 level) {
  u32* page_dir_ptr_tab = kmalloc_alignment(
      sizeof(u32) * PAGE_DIR_NUMBER, PAGE_SIZE * 4, KERNEL_TYPE);
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
  u32* kpage = (u32*)page_kernel_dir();
  // kprintf("page clone %x to %x\n",old_page,new_page);
  for (int l1_index = 0; l1_index < 4096; l1_index++) {
    u32* l2 = ((u32)l1[l1_index]) & 0xFFFFFC00;
    if (l2 != NULL) {
      u32 va_base = (u32)l1_index << 20;
      /*
       * Share kernel/identity L2 below EXEC_ADDR. Deep-copying ~100MB of
       * DRAM identity on every fork (infones then gui) allocates 100+ L2
       * tables and stalls the system under load.
       */
      if (kpage != NULL && va_base < (u32)EXEC_ADDR && kpage[l1_index] != 0) {
        new_l1[l1_index] = kpage[l1_index];
        continue;
      }
      page_dir_t* new_l2 =
          kmalloc_alignment(256 * sizeof(u32), 0x1000, KERNEL_TYPE);
      if (new_l2 == NULL) {
        kprintf("page_copy: alloc L2 failed at l1=%d\n", l1_index);
        return;
      }
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
  u32* page_dir_ptr_tab = page_create(level);
  if (page_dir_ptr_tab == NULL) {
    kprintf("page_clone: page_create failed\n");
    return NULL;
  }
  page_copy(old_page_dir, page_dir_ptr_tab);
  return page_dir_ptr_tab;
}

void page_map_on(page_dir_t* l1, u32 virtualaddr, u32 physaddr, u32 flags) {
  /* 守卫：DRAM 绝不该以 flags=0 映射（flags=0 ⇒ 描述符 = L2_DESC = 0x432 ⇒
   * TEX=000,C=0,B=0 = Strongly-ordered ⇒ 该页完全不进 cache）。
   * 实机 t113 曾因缺 page_map_on 原型（u64 flags 落到 r3）导致整机 7fps；
   * 该根因已在 pmemory.h/mm.h 修正（补声明），这里只留一个默认关闭的守卫：
   * 需要复查时定义 MMAP_ATTR_DEBUG 即可打印肇事调用者（addr2line 定位）。 */
#ifdef MMAP_ATTR_DEBUG
  if (flags == 0 && physaddr >= 0x40000000u && physaddr < 0x80000000u) {
    static u32 dbg_nc;
    if (dbg_nc < 8u) {
      dbg_nc++;
      log_error("MAP_NC_RAM va=%x pa=%x lr=%x\n", virtualaddr, physaddr,
                (u32)__builtin_return_address(0));
    }
  }
#endif
  // kprintf("map page %x vaddr:%x paddr:%x\n",l1,virtualaddr,physaddr);
  u32 l1_index = virtualaddr >> 20;
  u32 l2_index = virtualaddr >> 12 & 0xFF;
  u32* l2 = ((u32)l1[l1_index]) & 0xFFFFFC00;
  if (l2 == NULL) {
    l2 = kmalloc_alignment(256 * sizeof(u32), 0x1000, KERNEL_TYPE);
    l1[l1_index] = (((u32)l2) & 0xFFFFFC00) | L1_DESC;
    dccmvac((unsigned long)&l1[l1_index]);
  }
  l2[l2_index] = ((physaddr >> 12) << 12) | L2_DESC | flags;
  dccmvac((unsigned long)&l2[l2_index]);
  dmb();
  tlbimva(virtualaddr);
  dsb();
  isb();
}

void page_unmap_on(page_dir_t* page, u32 virtualaddr) {
  u32* l1 = page;
  u32 l1_index = virtualaddr >> 20;
  u32 l2_index = virtualaddr >> 12 & 0xFF;
  u32* l2 = ((u32)l1[l1_index]) & 0xFFFFFC00;
  if (l2 != NULL) {
    // l1[l1_index] = 0;
    l2[l2_index] = 0;
    dccmvac((unsigned long)&l2[l2_index]);
    dmb();
    tlbimva(virtualaddr);
    dsb();
    isb();
  }
}

void* page_v2p(u64* page, void* vaddr) {
  if (page == NULL) {
    kprintf("page v2p page is null\n");
  }
  void* phyaddr = NULL;
  u32* l1 = (u32*)page;
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

void mm_page_enable(u32 page_dir) {
#ifdef MP_ENABLE
  dccmvac((unsigned long)&page_dir);
  dsb();
  sev();   
#endif
  // cpu_disable_page();
  // cpu_icache_disable();

  // cp15_invalidate_icache();
  // cpu_invalid_tlb();

  cpu_set_domain(0x07070707);
  // cpu_set_domain(0xffffffff);
  // cpu_set_domain(0x55555555);

  cpu_set_page(page_dir);
  // start_dump();
  kprintf("enable page\n");
  cpu_enable_page();
  kprintf("paging success\n");
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

void mm_init_default(u32 kernel_page_dir){
  
}