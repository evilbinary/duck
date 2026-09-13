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

/* 【页表必须对 MMU 遍历器可见】遍历器不读 L1 D-cache，只读 DRAM/L2。
 * 内核堆可缓存后，页表描述符若只留在 cache 里，切页表那一刻遍历器就会读到
 * 陈旧 DRAM ⇒ 取指/翻译失败。步长 32B：按 MVA 的维护作用于"所在行"。 */
static void page_table_clean(unsigned long addr, u32 bytes) {
  for (u32 off = 0; off < bytes; off += 32) {
    dccmvac(addr + off);
  }
  dmb();
}

u32* page_create(u32 level) {
  u32* page_dir_ptr_tab =
      mm_alloc_zero_align(sizeof(u32) * PAGE_DIR_NUMBER, PAGE_SIZE * 4);
  if (page_dir_ptr_tab != NULL) {
    /* 整表（含未写入的 0 项）clean 到 PoC，见 page_table_clean 的说明。 */
    page_table_clean((unsigned long)page_dir_ptr_tab,
                     sizeof(u32) * PAGE_DIR_NUMBER);
  }
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
      /* 未写入项必须是 0（遍历器直接读 DRAM）。这里用显式循环而不是 kmemset：
       * 本文件不含声明 kmemset 的头，避免隐式声明。 */
      for (int k = 0; k < 256; k++) {
        new_l2[k] = 0;
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
      /* 【关键】新 L2 表整表 clean 到 PoC，否则切表后遍历器读到陈旧 DRAM。 */
      page_table_clean((unsigned long)new_l2, 256 * sizeof(u32));
    }
  }
  /* 新 L1 整表 clean（含未写入的 0 项）。 */
  page_table_clean((unsigned long)new_l1, sizeof(u32) * PAGE_DIR_NUMBER);
}

u32* page_clone(u32* old_page_dir, u32 level) {
  u32* page_dir_ptr_tab = page_create(level);
  page_copy(old_page_dir, page_dir_ptr_tab);
  return page_dir_ptr_tab;
}

void page_map_on(page_dir_t* l1, u32 virtualaddr, u32 physaddr, u32 flags) {
  // kprintf("map page %x vaddr:%x paddr:%x\n",l1,virtualaddr,physaddr);
  u32 l1_index = virtualaddr >> 20;
  u32 l2_index = virtualaddr >> 12 & 0xFF;
  u32* l2 = ((u32)l1[l1_index]) & 0xFFFFFC00;
  if (l2 == NULL) {
    l2 = mm_alloc_zero_align(256 * sizeof(u32), 0x1000);
    kmemset(l2, 0, 256 * sizeof(u32));
    l1[l1_index] = (((u32)l2) & 0xFFFFFC00) | L1_DESC;
    dccmvac((unsigned long)&l1[l1_index]);
  }
  l2[l2_index] = ((physaddr >> 12) << 12) | L2_DESC | flags;
  dccmvac((unsigned long)&l2[l2_index]);
  dmb();
  /* 【必须作废该 VA 的 TLB 项】否则旧表项会继续指向旧的物理页 —— 该页可能
   * 已被 vfree 回收、被内核堆复用，进程就会跑到错的物理页上取指/取数。
   * 实测症状：连续第二次 exec 时 PREF ABORT（跳去未映射的 0x1000）。
   * armv7-a 的同名函数一直有这三行，armv5 是移植时漏掉了。 */
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
    /* 解映射同样要作废 TLB 项（理由见 page_map_on）：否则 vfree 之后
     * 这个 VA 还在用旧翻译，页被复用时会串到别人的数据/代码。 */
    tlbimva(virtualaddr);
    dsb();
    isb();
  }
}

/* 【归还整棵用户页表】32 位短描述符格式（同 armv7-a，见其 mm.c 的
 * page_destroy 注释）。armv5 的 L1/L2 均为 mm_alloc_zero_align 直配，
 * kfree_alignment 经 kpage_v2p + mm_free_align 同样配对（内核堆恒等映射）。 */
void page_destroy(u32* upage) {
  if (upage == NULL) {
    return;
  }
  u32 start = (u32)EXEC_ADDR >> 20;
  u32 end = 0x80000000u >> 20;
  for (u32 i = start; i < end; i++) {
    u32 e = upage[i];
    if ((e & 1) == 0) {
      continue; /* 该 1MB 区间没有 L2 表 */
    }
    u32* l2 = (u32*)(e & 0xFFFFFC00u);
    for (u32 j = 0; j < 256; j++) {
      if ((l2[j] & 3) != 0) {
        void* pg = (void*)(l2[j] & 0xFFFFF000u);
        l2[j] = 0;
        mm_free_page(pg);
      }
    }
    upage[i] = 0;
    kfree_alignment(l2);
  }
  kfree_alignment(upage);
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
  kprintf("mm enable page start\n");
  cpu_disable_page();
  // cpu_icache_disable();
  cp15_invalidate_icache();
  cpu_invalid_tlb();
  cpu_set_domain(0x07070707);
  // cpu_set_domain(0xffffffff);
  // cpu_set_domain(0x55555555);

  cpu_set_page(page_dir);
  // start_dump();
  kprintf("enable page\n");
  cpu_enable_page();
  kprintf("paging success\n");

  // mm_test();
}

void mm_test() {
  u32* p = 0x9000f000;
  u32* addr = mm_alloc_zero_align(PAGE_SIZE,PAGE_SIZE);
  page_map(p, addr, 0);
  kprintf("test page_map %x %x\n",p,addr);

  *addr = 0x123456;
  if (*p != *addr) {
    kprintf("mmap test error\n");
    kprintf("===============> %x %x\n", *addr, *p);
  }

  // page_map(0x90000,0x600000,3);
  // u32* addr=mm_alloc(256);
  // *addr=0x123456;
  // kprintf("===============+>\n");
  // u32 *p = 0x1c2ac0c;
  // *p = 1 << 6;
  // kprintf("p=%x\n", *p);
}

void mm_init_default(u32 kernel_page_dir) {}