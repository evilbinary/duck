/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef ARM_MM_H
#define ARM_MM_H

#include "libs/include/types.h"

// ARM64 Page Table Entry bits
// For block and page descriptors

// Attribute fields
#define PTE_VALID       (1UL << 0)      // Entry is valid
#define PTE_TABLE       (1UL << 1)      // Entry is a table descriptor
#define PTE_PAGE        (1UL << 1)      // Entry is a page descriptor (for level 3)
#define PTE_BLOCK       (0UL << 1)      // Entry is a block descriptor

// Memory attributes index (MAIR_EL1)
#define PTE_ATTR_IDX(x)     ((u64)(x) << 2)
#define PTE_ATTR_NORMAL     PTE_ATTR_IDX(0)  // Normal memory
#define PTE_ATTR_DEVICE     PTE_ATTR_IDX(1)  // Device memory
#define PTE_ATTR_NORMAL_NC  PTE_ATTR_IDX(2)  // Normal non-cacheable

// Access permissions
#define PTE_AP_RW       (0UL << 6)      // Read/Write
#define PTE_AP_RO       (1UL << 6)      // Read-only
#define PTE_AP_RW_USER  (2UL << 6)      // Read/Write, user access
#define PTE_AP_RO_USER  (3UL << 6)      // Read-only, user access

// Shareability
#define PTE_SH_NS       (0UL << 8)      // Non-shareable
#define PTE_SH_OS       (2UL << 8)      // Outer shareable
#define PTE_SH_IS       (3UL << 8)      // Inner shareable

// Access flag
#define PTE_AF          (1UL << 10)     // Access flag

// Execute-never bits
#define PTE_PXN         (1UL << 53)     // Privileged execute-never
#define PTE_UXN         (1UL << 54)     // User execute-never

// Physical address mask (bits 12-47 for 4KB granule)
#define PTE_ADDR_MASK   (0x0000FFFFFFFFF000UL)

// Page table levels for 4KB pages, 39-bit VA (3-level: PGD->PMD->PTE)
// Similar to ARMv7-A 2-level structure
#define PTE_SHIFT       12              // 4KB pages
#define PMD_SHIFT       21              // 2MB blocks
#define PGD_SHIFT       30              // 1GB sections

// Number of entries per table (512 for 4KB pages)
#define PTRS_PER_TABLE  512

// TCR_EL1 field definitions - 39-bit VA with 16KB granule
#define TCR_T0SZ(x)     ((u64)((64 - (x)) & 0x3F))
#define TCR_T1SZ(x)     ((u64)((64 - (x)) & 0x3F) << 16)
#define TCR_IRGN0(x)    ((u64)(x) << 8)
#define TCR_ORGN0(x)    ((u64)(x) << 10)
#define TCR_SH0(x)      ((u64)(x) << 12)
#define TCR_TG0_4K      (0ULL << 14)
#define TCR_TG0_16K     (1ULL << 14)
#define TCR_TG0_64K     (2ULL << 14)
#define TCR_TG1_4K      (2ULL << 30)    // TTBR1 4KB granule (TG1: 10=4K)
#define TCR_TG1_16K     (1ULL << 30)
#define TCR_TG1_64K     (3ULL << 30)
#define TCR_IPS(x)      ((u64)(x) << 32)

// ARM64 AP bits (PTE[7:6]):
//   AP=0b00  → EL1 RW,  EL0 no access  (kernel-only pages)
//   AP=0b01  → EL1 RO,  EL0 no access
//   AP=0b10  → EL1+EL0 RW              (user-accessible pages)
//   AP=0b11  → EL1+EL0 RO
//
// PSTATE.PAN (bit21): when set, EL1 faults on AP=0b10/11 pages.
// We disable PAN in mm_page_enable via SCTLR_EL1.SPAN so EL1 can
// access user-mapped pages (needed for kernel↔user data copies).
// Kernel pages use AP=0b00 so they remain inaccessible from EL0.
#define PAGE_P          PTE_VALID
#define PAGE_RW         (PTE_VALID | PTE_TABLE | PTE_AF | PTE_AP_RW  | PTE_SH_IS | PTE_ATTR_NORMAL)
#define PAGE_RX         (PTE_VALID | PTE_TABLE | PTE_AF | PTE_AP_RO  | PTE_SH_IS | PTE_ATTR_NORMAL)
#define PAGE_RWX        (PTE_VALID | PTE_TABLE | PTE_AF | PTE_AP_RW  | PTE_SH_IS | PTE_ATTR_NORMAL)
#define PAGE_RO         (PTE_VALID | PTE_TABLE | PTE_AF | PTE_AP_RO  | PTE_SH_IS | PTE_ATTR_NORMAL)

// Device memory (non-shareable, device attribute, kernel-only)
#define PAGE_DEV        (PTE_VALID | PTE_TABLE | PTE_AF | PTE_AP_RW  | PTE_SH_OS | PTE_ATTR_DEVICE)

// User-accessible pages (EL0+EL1 RW/RO)
#define PAGE_USR_RW     (PTE_VALID | PTE_TABLE | PTE_AF | PTE_AP_RW_USER | PTE_SH_IS | PTE_ATTR_NORMAL)
#define PAGE_USR_RX     (PTE_VALID | PTE_TABLE | PTE_AF | PTE_AP_RO_USER | PTE_SH_IS | PTE_ATTR_NORMAL)
#define PAGE_USR_RWX    (PTE_VALID | PTE_TABLE | PTE_AF | PTE_AP_RW_USER | PTE_SH_IS | PTE_ATTR_NORMAL)

// Aliases for source compatibility with armv7-a callers
#define PAGE_SYS        PAGE_RW
#define PAGE_USR        PAGE_USR_RW

// Non-cacheable kernel pages
#define PAGE_RW_NC      (PTE_VALID | PTE_TABLE | PTE_AF | PTE_AP_RW  | PTE_SH_OS | PTE_ATTR_NORMAL_NC)

// Page directory type (64-bit entries)
typedef u64 page_dir_t;

// Function prototypes
u64* page_create(u32 level);
void page_copy(u64* old_page, u64* new_page);
u64* page_clone(u64* old_page_dir, u32 level);
void page_map_on(u64* pgd, u64 virtualaddr, u64 physaddr, u64 flags);
void page_unmap_on(u64* page, u64 virtualaddr);
void* page_v2p(u64* page_dir_ptr_tab, void* vaddr);
void mm_page_enable(u64 page_dir);
void mm_init_default(u64 kernel_page_dir);
void* page_kernel_dir(void);

// Helper to get index at each level (3-level: PGD->PMD->PTE)
static inline u64 pgd_index(u64 addr) { return (addr >> PGD_SHIFT) & 0x1FF; }
static inline u64 pmd_index(u64 addr) { return (addr >> PMD_SHIFT) & 0x1FF; }
static inline u64 pte_index(u64 addr) { return (addr >> PTE_SHIFT) & 0x1FF; }

#endif
