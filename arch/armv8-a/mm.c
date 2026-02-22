/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "mm.h"
#include "cpu.h"
#include "arch/pmemory.h"
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
