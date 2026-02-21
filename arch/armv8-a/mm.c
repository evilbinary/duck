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

// Allocate a zeroed 4KB-aligned page table (512 × 8 bytes = 4096 bytes)
u64* page_create(u32 level) {
  return (u64*)mm_alloc_zero_align(sizeof(u64) * PTRS_PER_TABLE, PAGE_SIZE);
}

// Deep copy a 3-level (PGD→PMD→PTE) page table, mirroring armv7-a page_copy.
// For each valid PGD entry, allocate a new PMD table and deep-copy it.
// For each valid PMD entry, allocate a new PTE table and copy leaf entries.
void page_copy(u64* old_pgd, u64* new_pgd) {
  if (old_pgd == NULL || new_pgd == NULL) {
    kprintf("page_copy: null argument\n");
    return;
  }

  for (int gi = 0; gi < PTRS_PER_TABLE; gi++) {
    u64 pgd_entry = old_pgd[gi];
    if (!(pgd_entry & PTE_VALID))
      continue;

    // Allocate a new PMD table
    u64* old_pmd = (u64*)(pgd_entry & PTE_ADDR_MASK);
    u64* new_pmd = (u64*)mm_alloc_zero_align(sizeof(u64) * PTRS_PER_TABLE, PAGE_SIZE);
    new_pgd[gi] = ((u64)new_pmd & PTE_ADDR_MASK) | PTE_VALID | PTE_TABLE;

    for (int mi = 0; mi < PTRS_PER_TABLE; mi++) {
      u64 pmd_entry = old_pmd[mi];
      if (!(pmd_entry & PTE_VALID))
        continue;

      // Allocate a new PTE table
      u64* old_pte = (u64*)(pmd_entry & PTE_ADDR_MASK);
      u64* new_pte = (u64*)mm_alloc_zero_align(sizeof(u64) * PTRS_PER_TABLE, PAGE_SIZE);
      new_pmd[mi] = ((u64)new_pte & PTE_ADDR_MASK) | PTE_VALID | PTE_TABLE;

      // Copy leaf entries (share physical pages, like armv7-a)
      for (int pi = 0; pi < PTRS_PER_TABLE; pi++) {
        if (old_pte[pi] & PTE_VALID) {
          new_pte[pi] = old_pte[pi];
        }
      }
    }
  }
}

// Clone page table
u64* page_clone(u64* old_pgd, u32 level) {
  u64* new_pgd = page_create(level);
  if (new_pgd == NULL) return NULL;
  page_copy(old_pgd, new_pgd);
  return new_pgd;
}

// Get or create next-level table at index
static u64* get_next_table(u64* table, u64 index, u8 create) {
  u64 entry = table[index];

  if (entry & PTE_VALID) {
    if (entry & PTE_TABLE)
      return (u64*)(entry & PTE_ADDR_MASK);
    return NULL;  // block entry, can't traverse
  }

  if (!create) return NULL;

  u64* new_table = page_create(0);
  if (new_table == NULL) return NULL;

  table[index] = ((u64)new_table & PTE_ADDR_MASK) | PTE_VALID | PTE_TABLE;
  return new_table;
}

// Map virtualaddr → physaddr with flags (3-level walk)
void page_map_on(u64* pgd, u64 virtualaddr, u64 physaddr, u64 flags) {
  if (pgd == NULL) {
    kprintf("page_map_on: pgd null\n");
    return;
  }

  u64* pmd = get_next_table(pgd, pgd_index(virtualaddr), 1);
  if (pmd == NULL) return;
  u64* pte = get_next_table(pmd, pmd_index(virtualaddr), 1);
  if (pte == NULL) return;

  pte[pte_index(virtualaddr)] = (physaddr & PTE_ADDR_MASK) | flags | PTE_VALID;

  asm volatile("tlbi vaae1, %0" : : "r"(virtualaddr >> 12) : "memory");
  dsb();
}

// Unmap virtualaddr
void page_unmap_on(u64* pgd, u64 virtualaddr) {
  if (pgd == NULL) return;

  u64* pmd = get_next_table(pgd, pgd_index(virtualaddr), 0);
  if (pmd == NULL) return;
  u64* pte = get_next_table(pmd, pmd_index(virtualaddr), 0);
  if (pte == NULL) return;

  pte[pte_index(virtualaddr)] = 0;

  asm volatile("tlbi vaae1, %0" : : "r"(virtualaddr >> 12) : "memory");
  dsb();
}

// Translate virtual → physical
void* page_v2p(u64* pgd, void* vaddr) {
  if (pgd == NULL) return NULL;

  u64 addr = (u64)vaddr;
  u64* pmd = get_next_table(pgd, pgd_index(addr), 0);
  if (pmd == NULL) return NULL;
  u64* pte = get_next_table(pmd, pmd_index(addr), 0);
  if (pte == NULL) return NULL;

  u64 entry = pte[pte_index(addr)];
  if (!(entry & PTE_VALID)) return NULL;

  return (void*)((entry & PTE_ADDR_MASK) | (addr & (PAGE_SIZE - 1)));
}

// Setup MAIR_EL1
static void setup_mair(void) {
  u64 mair =
    (0xFFUL <<  0) |   // Attr0: Normal memory, WB/WA
    (0x04UL <<  8) |   // Attr1: Device nGnRE
    (0x44UL << 16);    // Attr2: Normal non-cacheable
  asm volatile("msr mair_el1, %0" : : "r"(mair) : "memory");
}

// Enable MMU with given page table
void mm_page_enable(u64 page_dir) {
  setup_mair();

  // TCR_EL1: 4KB granule, 39-bit VA, inner/outer WB, inner-shareable
  u64 tcr =
    TCR_T0SZ(39) |
    TCR_IRGN0(1) |
    TCR_ORGN0(1) |
    TCR_SH0(3)   |
    TCR_TG0_4K   |
    TCR_IPS(1);

  asm volatile("msr tcr_el1, %0" : : "r"(tcr) : "memory");
  isb();

  asm volatile("msr ttbr0_el1, %0" : : "r"(page_dir) : "memory");
  isb();

  cpu_invalid_tlb();

  kprintf("enable page at %lx\n", page_dir);
  cpu_enable_page();
  kprintf("paging success\n");
}

void mm_init_default(u64 kernel_page_dir) {
  // Called after mm_page_enable; nothing more to do here.
}
