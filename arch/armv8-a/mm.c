/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "mm.h"
#include "cpu.h"
#include "arch/pmemory.h"
#include "kernel/memory.h"
#include "libs/include/kernel/common.h"

extern boot_info_t* boot_info;

static u64 v2p_internal(void* vaddr) {
    return (u64)vaddr;
}

u64* page_create(u32 level) {
  u64* table = (u64*)mm_alloc_zero_align(PAGE_SIZE, PAGE_SIZE);
  return table;
}

static u64* get_next_level(u64* table, u32 index, int alloc) {
  u64 entry = table[index];
  if (entry & PTE_VALID) {
    return (u64*)(entry & PTE_ADDR_MASK);
  }
  if (!alloc) return NULL;

  u64* next = page_create(0);
  if (next) {
    u64 paddr = v2p_internal(next);
    table[index] = (paddr & PTE_ADDR_MASK) | PTE_TYPE_TABLE | PTE_VALID;
  }
  return next;
}

void page_map_on(u64* pgd, u64 vaddr, u64 paddr, u64 flags) {
  if (!pgd) return;

  u64* pmd = get_next_level(pgd, pgd_index(vaddr), 1);
  if (!pmd) return;

  u64* pte = get_next_level(pmd, pmd_index(vaddr), 1);
  if (!pte) return;

  pte[pte_index(vaddr)] = (paddr & PTE_ADDR_MASK) | flags | PTE_TYPE_PAGE | PTE_VALID | PTE_AF;

  asm volatile("dsb sy" : : : "memory");
  asm volatile("tlbi vaae1is, %0" : : "r"(vaddr >> 12) : "memory");
  asm volatile("dsb sy" : : : "memory");
  asm volatile("isb" : : : "memory");
}

void page_unmap_on(u64* pgd, u64 vaddr) {
  if (!pgd) return;

  u64* pmd = get_next_level(pgd, pgd_index(vaddr), 0);
  if (!pmd) return;

  u64* pte = get_next_level(pmd, pmd_index(vaddr), 0);
  if (!pte) return;

  pte[pte_index(vaddr)] = 0;

  asm volatile("dsb sy" : : : "memory");
  asm volatile("tlbi vaae1is, %0" : : "r"(vaddr >> 12) : "memory");
  asm volatile("dsb sy" : : : "memory");
  asm volatile("isb" : : : "memory");
}

void* page_v2p(u64* pgd, void* vaddr) {
  u64 addr = (u64)vaddr;
  if (!pgd) return NULL;
  u64* pmd = get_next_level(pgd, pgd_index(addr), 0);
  if (!pmd) return NULL;
  u64* pte = get_next_level(pmd, pmd_index(addr), 0);
  if (!pte) return NULL;
  u64 entry = pte[pte_index(addr)];
  if (!(entry & PTE_VALID)) return NULL;
  return (void*)((entry & PTE_ADDR_MASK) | (addr & (PAGE_SIZE - 1)));
}

void page_copy(u64* old_pgd, u64* new_pgd) {
  for (int i = 0; i < PTRS_PER_TABLE; i++) {
    if (old_pgd[i] & PTE_VALID) {
      u64* old_pmd = (u64*)(old_pgd[i] & PTE_ADDR_MASK);
      u64* new_pmd = page_create(0);
      u64 p_pmd = v2p_internal(new_pmd);
      new_pgd[i] = (p_pmd & PTE_ADDR_MASK) | PTE_TYPE_TABLE | PTE_VALID;
      for (int j = 0; j < PTRS_PER_TABLE; j++) {
        if (old_pmd[j] & PTE_VALID) {
          u64* old_pte = (u64*)(old_pmd[j] & PTE_ADDR_MASK);
          u64* new_pte = page_create(0);
          u64 p_pte = v2p_internal(new_pte);
          new_pmd[j] = (p_pte & PTE_ADDR_MASK) | PTE_TYPE_TABLE | PTE_VALID;
          for (int k = 0; k < PTRS_PER_TABLE; k++) {
            if (old_pte[k] & PTE_VALID) {
              new_pte[k] = old_pte[k] | PTE_AF;
            }
          }
        }
      }
    }
  }
}

u64* page_clone(u64* old_pgd, u32 level) {
  u64* new_pgd = page_create(level);
  if (new_pgd) page_copy(old_pgd, new_pgd);
  return new_pgd;
}

/* 【归还整棵用户页表】AArch64：3 级表（pgd→pmd→pte），每级 512 项 u64，
 * 4KB 粒度。有效描述符 bits[1:0]=0b11（表/页），块描述符 0b01 跳过（只漏不踩）。
 * 表由 page_create()→mm_alloc_zero_align(PAGE_SIZE,PAGE_SIZE) 分配，
 * 用 kfree_alignment 归还（内核堆恒等映射，配对正确）。
 * 【只扫用户区 [EXEC_ADDR, 0x80000000)】上界必须与 32 位架构
 * （armv7-a/armv5/armv7 的 page_destroy，raspi2 真机长期验证）一致：
 * 0x80000000 之上是设备/帧缓冲别名（xwin 的 fb 别名 0xfb000000）与内核
 * 共享映射，其物理页是 MMIO/GPU 显存、不属于页分配器 —— 一旦归还，
 * mm_free_page 会直接往设备物理地址写空闲链指针（raspi3 实测：
 * VA 0xfb000000→PA 0x3c100000 被归还，mm_free_page 的 str 触发翻译错误
 * 崩溃，并把 0x3f000000 外设区也串进空闲链）。pgd[0]（VA 0-1GB）同理含
 * 内核恒等映射与 MMIO，也不扫。
 * 【起点必须按 VA 过滤，不能只按 pgd 索引】pgd 粒度是 1GB，而
 * EXEC_ADDR(0x60000000) 不在 1GB 边界上：起点 pgd[1] 覆盖
 * VA 0x40000000-0x7FFFFFFF，其中 [0x40000000,0x60000000) 属于 EXEC_ADDR
 * 之下的空洞 —— 平台代码在那里有【合法的设备映射】（BCM2837 的 core-local
 * 中断寄存器 CORE0_TIMER_IRQCNTL = 0x40000040，platform_map() 以 PAGE_DEV
 * 映射 VA=PA=0x40000000）。若不过滤 VA，这一页会被当普通页归还：
 * mm_free_page 把 PA 0x40000000 推进空闲链（链指针就写在被释放页头部），
 * 下一次 mm_alloc_page 即把它当用户页发出去，写它立刻是同步外部中止
 * （raspi3 实测：`valloc: phy 40000000 beyond RAM end`、`ls` 直接失败，
 * 再往前就是缺页风暴）。32 位架构按 1MB L1 索引扫描（start=EXEC_ADDR>>20）
 * 天然精确，不存在这个空洞。
 * 【严禁】用 32 位语义（u32*、1MB section、0xFFFFF000）解析 64 位页表：
 * 索引/掩码全错，会把堆内存当页表项，垃圾指针进 mm_page_free_list，
 * 被 page_create 清零后物理擦掉 .text（raspi3 实测内核崩死）。 */
void page_destroy(u64* upage) {
  if (upage == NULL) {
    return;
  }
  u32 start = (u32)(EXEC_ADDR >> PGD_SHIFT);  /* 1GB/项，用户区起点 */
  u32 end = (u32)(0x80000000UL >> PGD_SHIFT); /* 用户区上界（排他） */
  for (u32 i = start; i < end; i++) {
    u64 e1 = upage[i];
    if ((e1 & 3) != 3) {
      continue; /* 无效或块描述符 */
    }
    u64* pmd = (u64*)(e1 & PTE_ADDR_MASK);
    for (u32 j = 0; j < PTRS_PER_TABLE; j++) {
      u64 e2 = pmd[j];
      if ((e2 & 3) != 3) {
        continue;
      }
      u64* pte = (u64*)(e2 & PTE_ADDR_MASK);
      for (u32 k = 0; k < PTRS_PER_TABLE; k++) {
        u64 e3 = pte[k];
        if ((e3 & 3) != 3) {
          continue;
        }
        vaddr_t va = ((vaddr_t)i << PGD_SHIFT) | ((vaddr_t)j << PMD_SHIFT) |
                     ((vaddr_t)k << PTE_SHIFT);
        pte[k] = 0;
        /* pgd 1GB 粒度带来的低 VA 空洞（含 0x40000000 的设备映射）：不归还 */
        if (va < (vaddr_t)EXEC_ADDR || va >= (vaddr_t)0x80000000UL) {
          continue;
        }
        /* 兜底：物理页不在 boot_info 声明的 RAM 内（设备/未认领映射）→ 不归还 */
        void* pg = (void*)(e3 & PTE_ADDR_MASK);
        if (!mm_page_in_ram(pg)) {
          continue;
        }
        mm_free_page(pg);
      }
      pmd[j] = 0;
      kfree_alignment(pte);
    }
    upage[i] = 0;
    kfree_alignment(pmd);
  }
  kfree_alignment(upage);
}

void mm_page_enable(u64 page_dir) {
  u64 mair = (0xFFUL << 0) | (0x04UL << 8) | (0x44UL << 16);
  asm volatile("msr mair_el1, %0" : : "r"(mair));

  u64 tcr = TCR_T0SZ(39) | TCR_T1SZ(39) | TCR_IRGN0_WBWA | TCR_ORGN0_WBWA | 
            TCR_SH0_INNER | TCR_TG0_4KB | TCR_TG1_4KB | TCR_IPS_40BIT;
  asm volatile("msr tcr_el1, %0" : : "r"(tcr));
  asm volatile("isb");

  u64 p_pgd = v2p_internal((void*)page_dir);
  asm volatile("msr ttbr0_el1, %0" : : "r"(p_pgd));
  asm volatile("isb");

  asm volatile("tlbi vmalle1is");
  asm volatile("dsb sy");
  asm volatile("isb");

  u64 sctlr;
  asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
  // SPAN=1, I=1, M=1. Disable Data Cache (C=0) for stability during early boot.
  // Clear WXN (19), UWXN (20), A (1), SA (3) to avoid unnecessary faults.
  sctlr &= ~((1UL << 19) | (1UL << 20) | (1UL << 2) | (1UL << 1) | (1UL << 3));
  sctlr |= (1UL << 0) | (1UL << 12) | (1UL << 23);
  asm volatile("msr sctlr_el1, %0" : : "r"(sctlr));
  asm volatile("dsb sy");
  asm volatile("isb");

  kprintf("VMSAv8-64 MMU enabled at %lx\n", p_pgd);
}

void mm_init_default(void) {
}

#ifdef CONFIG_COW
/* ---- COW 接口：本架构暂未实现，给空实现保证 kernel 侧可链接 ----
 * （armv5 已有真实实现，其余平台按需补齐） */
int page_fault_is_write(void) { return 0; }
int page_cow_query(u32* upage, u32 va, u32* pa) { (void)upage; (void)va; (void)pa; return 0; }
void page_cow_apply(u32* upage, u32 va, u32 new_pa) { (void)upage; (void)va; (void)new_pa; }
#endif
