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
 * kfree_alignment 经 kpage_v2p + mm_free_align 同样配对（内核堆恒等映射）。
 *
 * 【ARMv5(VIVT) 必须按 VA 失效缓存】ARM926 的 D-cache 以虚拟地址为 tag，
 * 进程退出时这里把用户页还给页分配器，若不按 VA clean+invalidate，该物理页
 * 被下一个进程在**相同的 VA**（同样的栈/堆地址）复用时，CPU 可能直接命中
 * 已死进程残留的脏 cache 行 —— 新进程读到旧数据（栈/分配器元数据），表现为
 * mallocng 元数据自相矛盾、指针跳飞。armv7-a 是 PIPT，按物理地址索引，没有
 * 这个问题（所以同一份代码在树莓派上正常）。vfree 侧早已按 VA+PA 维护，
 * 这里补齐进程回收这条路径。 */
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
        u32 va = (i << 20) | (j << 12);
        /* 先按用户 VA 与物理地址两侧 clean+invalidate，再归还物理页 */
        cpu_flush_dcache_range((unsigned long)va,
                               (unsigned long)va + PAGE_SIZE);
        cpu_flush_dcache_range((unsigned long)pg,
                               (unsigned long)pg + PAGE_SIZE);
        l2[j] = 0;
        mm_free_page(pg);
      }
    }
    upage[i] = 0;
    kfree_alignment(l2);
  }
  kfree_alignment(upage);
}

/* 虚拟地址 → 物理地址。
 *
 * 【修复】必须按 ARM 描述符类型位解释 L1 项：
 *   bits[1:0]==0b01 粗页表；==0b10 段(1MB)；==0b00 fault；==0b11 保留。
 * 原实现无条件把 L1 项当 L2 基址（`& 0xFFFFFC00`），有两个问题：
 *   1) 段映射（内核用来映射 RAM/设备）会被当成"L2 指针"，于是把设备寄存器/
 *      数据当页表读，算出垃圾 PA（此前 `remap memory fault ... phy: ff000000`
 *      里那个既不是 RAM 也不是外设的"物理地址"就是这么来的）；
 *   2) fault/保留项本应返回 NULL 走"未映射→需求分配"，却可能被当成
 *      "已映射却缺页"而把线程杀掉。
 * L2 项同理：bits[1:0]==0b00 是 fault，0b11 是保留，都按未映射处理。 */
void* page_v2p(u64* page, void* vaddr) {
  if (page == NULL) {
    kprintf("page v2p page is null\n");
    return NULL;
  }
  void* phyaddr = NULL;
  u32* l1 = (u32*)page;
  u32 l1_index = (u32)vaddr >> 20;
  u32 l2_index = (u32)vaddr >> 12 & 0xFF;
  u32 offset = (u32)vaddr & 0x0FFF;

  u32 l1e = l1[l1_index];
  if ((l1e & 3u) == 2u) { /* 1MB 段描述符：直接得到物理地址 */
    return (void*)((l1e & 0xFFF00000u) + offset);
  }
  if ((l1e & 3u) != 1u) { /* 非粗页表：无效项，视为未映射 */
    return NULL;
  }
  u32* l2 = (u32*)(l1e & 0xFFFFFC00u);
  if (l2 == NULL) {
    return NULL;
  }
  u32 l2e = l2[l2_index];
  if ((l2e & 3u) == 0 || (l2e & 3u) == 3) { /* fault / 保留 */
    return NULL;
  }
  phyaddr = (void*)((l2e >> 12) << 12);
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