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

// Kernel page directory
static u64* kernel_pgd = NULL;

// Allocate a page table (512 entries, 4KB)
u64* page_create(u32 level) {
  u64* page_dir = (u64*)mm_alloc_zero_align(sizeof(u64) * PTRS_PER_TABLE, PAGE_SIZE);
  return page_dir;
}

// Copy page table
void page_copy(u64* old_page, u64* new_page) {
  if (old_page == NULL || new_page == NULL) {
    kprintf("page copy error: null page\n");
    return;
  }
  
  // Copy all entries
  for (int i = 0; i < PTRS_PER_TABLE; i++) {
    u64 entry = old_page[i];
    if (entry & PTE_VALID) {
      // Mark as read-only for copy-on-write
      // For now, just copy
      new_page[i] = entry;
    }
  }
}

// Clone a page table
u64* page_clone(u64* old_page_dir, u32 level) {
  u64* new_page_dir = page_create(level);
  if (new_page_dir == NULL) {
    return NULL;
  }
  
  page_copy(old_page_dir, new_page_dir);
  return new_page_dir;
}

// Get next level table, create if not exists
static u64* get_next_table(u64* table, u64 index, u8 create) {
  u64 entry = table[index];
  
  if (entry & PTE_VALID) {
    if (entry & PTE_TABLE) {
      return (u64*)(entry & PTE_ADDR_MASK);
    }
    // It's a block, can't traverse further
    return NULL;
  }
  
  if (!create) {
    return NULL;
  }
  
  // Create new table
  u64* new_table = page_create(0);
  if (new_table == NULL) {
    return NULL;
  }
  
  // Set as table descriptor
  table[index] = ((u64)new_table & PTE_ADDR_MASK) | PTE_VALID | PTE_TABLE;
  return new_table;
}

// Map a virtual address to physical address (3-level: PGD->PMD->PTE)
// Similar to ARMv7-A 2-level page table
void page_map_on(u64* pgd, u64 virtualaddr, u64 physaddr, u64 flags) {
  if (pgd == NULL) {
    kprintf("page_map_on: pgd is null\n");
    return;
  }
  
  // Get indices for each level (39-bit VA: PGD->PMD->PTE)
  u64 pgd_idx = pgd_index(virtualaddr);
  u64 pmd_idx = pmd_index(virtualaddr);
  u64 pte_idx = pte_index(virtualaddr);
  
  // Walk through levels
  u64* pmd = get_next_table(pgd, pgd_idx, 1);
  if (pmd == NULL) return;
  
  u64* pte = get_next_table(pmd, pmd_idx, 1);
  if (pte == NULL) return;
  
  // Set final PTE - ensure PTE_VALID is set
  pte[pte_idx] = (physaddr & PTE_ADDR_MASK) | flags | PTE_VALID;
  
  // Invalidate TLB for this address
  asm volatile("tlbi vaae1, %0" : : "r"(virtualaddr >> 12) : "memory");
}

// Unmap a virtual address (3-level: PGD->PMD->PTE)
void page_unmap_on(u64* pgd, u64 virtualaddr) {
  if (pgd == NULL) return;
  
  u64 pgd_idx = pgd_index(virtualaddr);
  u64 pmd_idx = pmd_index(virtualaddr);
  u64 pte_idx = pte_index(virtualaddr);
  
  u64* pmd = get_next_table(pgd, pgd_idx, 0);
  if (pmd == NULL) return;
  
  u64* pte = get_next_table(pmd, pmd_idx, 0);
  if (pte == NULL) return;
  
  // Clear PTE
  pte[pte_idx] = 0;
  
  // Invalidate TLB
  asm volatile("tlbi vaae1, %0" : : "r"(virtualaddr >> 12) : "memory");
}

// Translate virtual address to physical (3-level: PGD->PMD->PTE)
void* page_v2p(u64* page_dir_ptr_tab, void* vaddr) {
  u64* pgd = page_dir_ptr_tab;
  u64 addr = (u64)vaddr;
  
  if (pgd == NULL) return NULL;
  
  u64 pgd_idx = pgd_index(addr);
  u64 pmd_idx = pmd_index(addr);
  u64 pte_idx = pte_index(addr);
  
  u64* pmd = get_next_table(pgd, pgd_idx, 0);
  if (pmd == NULL) return NULL;
  
  u64* pte = get_next_table(pmd, pmd_idx, 0);
  if (pte == NULL) return NULL;
  
  u64 entry = pte[pte_idx];
  if (!(entry & PTE_VALID)) return NULL;
  
  u64 phys = entry & PTE_ADDR_MASK;
  u64 offset = addr & (PAGE_SIZE - 1);
  
  return (void*)(phys | offset);
}

// Set up MAIR_EL1 (Memory Attribute Indirection Register)
static void setup_mair(void) {
  u64 mair = 
    (0xFFUL << 0) |   // Attr0: Normal memory, Outer WB, Inner WB
    (0x04UL << 8) |   // Attr1: Device memory, nGnRE
    (0x44UL << 16);   // Attr2: Normal memory, non-cacheable
  
  asm volatile("msr mair_el1, %0" : : "r"(mair) : "memory");
}

// Enable MMU
void mm_page_enable(u64 page_dir) {
  // Setup MAIR
  setup_mair();
  
  // Set TCR_EL1
  // 4KB granule, Inner/Outer WB, Inner shareable, 39-bit VA (3-level page table)
  u64 tcr = 
    TCR_T0SZ(39) |          // 39-bit virtual address (3-level page table)
    TCR_IRGN0(1) |          // Inner WB
    TCR_ORGN0(1) |          // Outer WB
    TCR_SH0(3) |            // Inner shareable
    TCR_TG0_4K |            // 4KB granule
    TCR_IPS(1);             // 40-bit PA for RPi3
  
  asm volatile("msr tcr_el1, %0" : : "r"(tcr) : "memory");
  isb();
  
  // Set TTBR0
  asm volatile("msr ttbr0_el1, %0" : : "r"(page_dir) : "memory");
  isb();
  
  // Invalidate TLB
  cpu_invalid_tlb();
  
  kprintf("enable page at %x\n", page_dir);
  
  // Enable MMU and caches
  cpu_enable_page();
  
  kprintf("paging success\n");
}

// Initialize default page tables
void mm_init_default(u64 kernel_page_dir) {
  kernel_pgd = (u64*)kernel_page_dir;
  
  // Map kernel code, data, and device memory
  // This will be called by kernel initialization
}
