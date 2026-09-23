/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "arch/cpu.h"
#include "arch/display.h"
#include "arch/pmemory.h"
#include "kernel/memory.h"
#include "cpu.h"
#include "gpio.h"

#define PAGE_DIR_NUMBER 1024 * 1

extern boot_info_t* boot_info;
extern void dccmvac(unsigned long mva);
static u32 page_dir[PAGE_DIR_NUMBER] __attribute__((aligned(0x100)));

/* 【页表必须对 MMU 遍历器可见】遍历器不读 L1 D-cache，只读 DRAM/L2。
 * 内核堆可缓存后，页表描述符若只留在 cache 里，切页表那一刻遍历器就会
 * 读到陈旧 DRAM ⇒ 取指/翻译失败（t113 实测为"切页表后串口再无输出"）。
 * 步长取 32B：按 MVA 的 cache 维护作用于"所在行"，行更宽时重复清同一行无害。 */
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
  if (new_page != NULL) {
    page_table_clean((unsigned long)new_page, sizeof(u32) * PAGE_DIR_NUMBER);
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
    dccmvac((unsigned long)&l1[l1_index]);
  }
  l2[l2_index] = ((physaddr >> 12) << 12) | L2_DESC | flags;
  dccmvac((unsigned long)&l2[l2_index]);
  dmb();
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
    dccmvac((unsigned long)&l2[l2_index]);
    dmb();
  }
}

/* 【归还整棵用户页表】32 位短描述符格式（同 armv7-a，见其 mm.c 的
 * page_destroy 注释）。L1/L2 均为 mm_alloc_zero_align 直配，
 * kfree_alignment 经 kpage_v2p + mm_free_align 配对（内核堆恒等映射）。 */
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
    dccmvac((unsigned long)&l2[l2_index]);
    dmb();
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
#ifdef CONFIG_COW
/* ---- COW 接口：本架构暂未实现，给空实现保证 kernel 侧可链接 ----
 * （armv5 已有真实实现，其余平台按需补齐） */
int page_fault_is_write(void) { return 0; }
int page_cow_query(u32* upage, u32 va, u32* pa) { (void)upage; (void)va; (void)pa; return 0; }
void page_cow_apply(u32* upage, u32 va, u32 new_pa) { (void)upage; (void)va; (void)new_pa; }
#endif
