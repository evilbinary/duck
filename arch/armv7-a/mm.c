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
#include "kernel/page_ref.h"

#define PAGE_DIR_NUMBER 4096

extern boot_info_t* boot_info;

/* page_put_page：kernel 侧注册的"该页是否允许释放"回调（COW 共享页 ref>1 不放）。
 * 它声明在 arch/pmemory.h 里、但被 #ifdef CONFIG_COW 包着；而本文件 include
 * pmemory.h 的时机早于 kernel/config.h（CONFIG_COW 经 kernel/memory.h 才可见）
 * ⇒ 那句声明会被跳过（编译报 undeclared）。这里显式声明一次，不依赖头文件时序。 */
extern int (*page_put_page)(void* pa);
extern memory_manager_t mmt;
extern void dccmvac(unsigned long mva);
extern void* page_kernel_dir(void);

u32* page_create(u32 level) {
  u32* page_dir_ptr_tab = kmalloc_alignment(
      sizeof(u32) * PAGE_DIR_NUMBER, PAGE_SIZE * 4, KERNEL_TYPE);
  if (page_dir_ptr_tab == NULL) {
    return NULL;
  }
  /* 【页表必须对 MMU 遍历器可见】
   * 遍历器不读 L1 D-cache，而是直接去 DRAM/L2 取描述符。内核堆现在可缓存
   * （PAGE_KMEM = WB），若新表只存在于 cache，遍历器读到的是陈旧 DRAM（或
   * 堆里残留的脏行）；一旦切 TTBR0 过去就会取指/翻译失败（实测 t113 表现
   * 为"切页表那一瞬串口再无输出"）。
   * 这里先整表清零再 clean&invalidate 到 PoC，让 DRAM 与 cache 一致；之后
   * 逐条写入的描述符由 page_map_on 逐行 clean。 */
  kmemset(page_dir_ptr_tab, 0, sizeof(u32) * PAGE_DIR_NUMBER);
  cpu_flush_dcache_range((unsigned long)page_dir_ptr_tab,
                         (unsigned long)page_dir_ptr_tab +
                             sizeof(u32) * PAGE_DIR_NUMBER);
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
      /* 未写入的项必须是 0（遍历器直接读 DRAM，堆里残留的旧内容会变成
       * "凭空映射"）。先清零，最后整表 clean 到 PoC。 */
      kmemset(new_l2, 0, 256 * sizeof(u32));
      new_l1[l1_index] = (((u32)new_l2) & 0xFFFFFC00) | L1_DESC;
      // kprintf("%d %x\n", l1_index, (u32)l2>>10 );
      for (int l2_index = 0; l2_index < 256; l2_index++) {
        u32* addr = l2[l2_index] >> 12;
        if (addr != NULL || l1_index == 0) {
          new_l2[l2_index] = l2[l2_index];
          // kprintf("  %d %x\n", l2_index, addr);
        }
      }
      /* 【关键】克隆出来的 L2 只写在 cache 里，必须 clean 到 PoC，
       * 否则切到这张表时 MMU 遍历器读到的是陈旧 DRAM（详见 page_create）。 */
      cpu_flush_dcache_range((unsigned long)new_l2,
                             (unsigned long)new_l2 + 256 * sizeof(u32));
    }
  }
  /* 新 L1 整表 clean：既覆盖上面写入的条目，也把共享内核项与(未写入的)
   * 0 值一并落到 DRAM —— 页表是"整张表"给遍历器用的，不能只清写过的行。 */
  cpu_flush_dcache_range((unsigned long)new_l1,
                         (unsigned long)new_l1 +
                             sizeof(u32) * PAGE_DIR_NUMBER);
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
   * 需要复查时构建加 -DMM_DEBUG_PROBE=1 即可打印肇事调用者（addr2line 定位）。 */
#if MM_DEBUG_PROBE
  if (flags == 0 && physaddr >= 0x40000000u && physaddr < 0x80000000u) {
    static u32 dbg_nc;
    if (dbg_nc < 8u) {
      dbg_nc++;
      log_error("MAP_NC_RAM va=%x pa=%x lr=%x\n", virtualaddr, physaddr,
                (u32)__builtin_return_address(0));
    }
  }
#endif
  /* 【属性决策唯一化】这里**不再**按虚拟地址区间二次改写属性。
   * 历史包袱：曾经为了"内核区非缓存保稳、用户区可缓存提性能"，在本函数里按 VA
   * 强行改写 flags。那样会让同一物理页在不同映射路径拿到不同属性（ARM 上属未
   * 定义行为），而且"属性决策"分散在两处、无法审计。
   * 现在属性只由调用点的用途名决定（PAGE_DEV / PAGE_KERNEL / PAGE_KMEM /
   * PAGE_USER / PAGE_FB / PAGE_SHARED），本函数只负责把 flags 原样拼进描述符。 */
  (void)virtualaddr;
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

/* 【归还整棵用户页表】32 位短描述符格式：L1 4096 项（1MB/项），L2 256 项
 * （4KB 小页）。只扫用户 VA 区间 [EXEC_ADDR, 0x80000000)：内核区
 * （0x82000000+）的页表是共享的，绝不能释放。
 * L2 走 kmalloc_alignment(KERNEL_TYPE)（VM 路径），用 kfree_alignment
 * 配对释放；L1（page_create 分配）同样 kfree_alignment。
 * 注意：进来的 upage 必须是本进程私有的（共享内核页目录由调用方跳过）。 */
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
        /* 【引用计数闸】COW 共享页（ref>1）不能在这里放掉 —— 另一个进程还在用。
         * armv5 早就有这道闸；armv7-a 之前是空实现（没有 COW）所以直接放，
         * 现在 COW 生效，必须同样按引用计数决定。 */
        if (page_put_page == NULL || page_put_page(pg) == 0) {
          mm_free_page(pg);
        }
      }
    }
    /* L2 表本身也归还：上面已把 256 项清空，表私有且已无用 */
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
#ifdef CONFIG_COW
/* ---- COW 接口：本架构暂未实现，给空实现保证 kernel 侧可链接 ----
 * （armv5 已有真实实现，其余平台按需补齐） */
/* ============ COW（写时复制）PTE 操作 ============
 * 与 armv5/mm.c 同构，但【不用软件标记位】：
 *   armv5 的 PTE bit9 是 IMP（可私用）⇒ 拿它当 COW 标记；
 *   而 armv7-a 的 bit9 是 AP[2]（只读位，扩展格式下 PL1 也会只读）
 *   ⇒ 照抄会让【内核自己也写不进去】。
 * 这里改用「用户只读(AP[1:0]=0b10) + 引用计数>1」识别 COW 页：
 *   · 保护：把可写页改成 AP[1:0]=0b10 = 特权可写 / 用户只读
 *     ⇒ 用户写触发 fault，内核（身份映射侧/特权写）仍可写 ✓
 *   · 识别：用户只读 + page_ref_get(pa) > 1（fork 时 kernel 侧 page_ref_inc 过）
 *   · 破写：PTE 指向新页并恢复 AP[1:0]=0b11
 * 引用计数由 kernel 侧管理（vmemory_clone / page.c 的 COW 路径）。 */

/* 取 VA 对应的小页 PTE（未映射/非小页返回 NULL） */
static u32* cow_l2_slot(u32* upage, u32 va) {
  u32 l1i = va >> 20;
  u32 l2i = (va >> 12) & 0xFF;
  u32 l1e = upage[l1i];
  u32* l2;
  if ((l1e & 0x3) == 0) {
    return NULL;
  }
  l2 = (u32*)(l1e & 0xFFFFFC00u);
  if (l2 == NULL) {
    return NULL;
  }
  if ((l2[l2i] & 0x3) == 0) {
    return NULL; /* 空项 */
  }
  return &l2[l2i];
}

/* read_dfsr() 定义在 armv7-a/cpu.c，但头文件没声明它（其它 .c 一直靠隐式声明，
 * 参数按无原型规则传，容易踩坑）⇒ 这里显式声明一次，供 COW 判断"写故障"。 */
u32 read_dfsr(void);

int page_fault_is_write(void) { return (read_dfsr() & (1u << 11)) ? 1 : 0; }

int page_cow_query(u32* upage, u32 va, u32* pa) {
  u32* s = cow_l2_slot(upage, va);
  u32 d;
  u32 p;
  if (s == NULL) {
    return 0;
  }
  d = *s;
  if (((d >> 4) & 0x3u) != 0x2u) {
    return 0; /* 不是"用户只读"，不是 COW 页 */
  }
  p = d & 0xFFFFF000u;
  if (page_ref_get(p) <= 1) {
    return 0; /* 没被共享（引用计数 <=1）⇒ 不是 COW */
  }
  if (pa != NULL) {
    *pa = p;
  }
  return 1;
}

void page_cow_apply(u32* upage, u32 va, u32 new_pa) {
  u32* s = cow_l2_slot(upage, va);
  u32 d;
  if (s == NULL) {
    return;
  }
  d = *s;
  if (new_pa == 0) {
    if (((d >> 4) & 0x3u) != 0x3u) {
      return; /* 本来就不让用户写（代码/rodata）⇒ 共享即可，无需 COW */
    }
    d = (d & ~0x30u) | (0x2u << 4); /* AP[1:0]=0b10 特权可写/用户只读 */
  } else {
    d = (d & ~0x30u) | (0x3u << 4);              /* 恢复用户可写 */
    d = (d & 0xFFFu) | (new_pa & 0xFFFFF000u);   /* 指向新页 */
  }
  *s = d;
  cpu_flush_dcache_range((unsigned long)s,
                         (unsigned long)s + sizeof(u32));
  tlbimva(va);
  dsb();
  isb();
}
#endif
