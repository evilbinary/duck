/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef ARM_MM_H
#define ARM_MM_H

#include "libs/include/types.h"

/*
 * ARMv8-A VMSAv8-64 Architecture Definitions (4KB Granule)
 * Compatibility layer for YiYiYa OS (mirrors armv7-a style)
 */

#define PTE_VALID         (1UL << 0)
#define PTE_TYPE_TABLE    (1UL << 1)   // L0-L2 table
#define PTE_TYPE_PAGE     (1UL << 1)   // L3 page
#define PTE_AF            (1UL << 10)  // Access Flag

// Attributes (MAIR_EL1)
#define PTE_ATTR_IDX(x)   (((u64)(x) & 0x7) << 2)
#define PTE_ATTR_NORMAL   PTE_ATTR_IDX(0)
#define PTE_ATTR_DEVICE   PTE_ATTR_IDX(1)
#define PTE_ATTR_NC       PTE_ATTR_IDX(2)

// Access Permissions (AP[2:1])
// 00: EL1 RW, EL0 None
// 01: EL1 RW, EL0 RW
// 10: EL1 RO, EL0 None
// 11: EL1 RO, EL0 RO
#define PTE_AP_EL1_RW     (0UL << 6)
#define PTE_AP_ANY_RW     (1UL << 6)
#define PTE_AP_EL1_RO     (2UL << 6)
#define PTE_AP_ANY_RO     (3UL << 6)

#define PTE_SH_INNER      (3UL << 8)
#define PTE_SH_OUTER      (2UL << 8)

#define PTE_AF            (1UL << 10)
#define PTE_PXN           (1UL << 53)
#define PTE_UXN           (1UL << 54)

#define PTE_ADDR_MASK     (0x0000FFFFFFFFF000UL)

#define PGD_SHIFT         30
#define PMD_SHIFT         21
#define PTE_SHIFT         12
#define PTRS_PER_TABLE    512

// Standard Page Types (Compatibility with armv7-a)
#define PAGE_P            PTE_VALID
#define PAGE_R            0

// To bypass the implicit PXN (Privileged Execute Never) rule where EL1 cannot execute 
// EL0-writable pages, we map ALL pages as EL1-only (PTE_AP_EL1_RW).
// We then run user threads in EL1t mode so they can access these pages.
#define PAGE_RW           (PTE_AP_EL1_RW | PTE_SH_INNER | PTE_ATTR_NORMAL)
#define PAGE_RX           (PTE_AP_EL1_RW | PTE_SH_INNER | PTE_ATTR_NORMAL)
#define PAGE_RWX          (PTE_AP_EL1_RW | PTE_SH_INNER | PTE_ATTR_NORMAL)

#define PAGE_RW_NC        (PTE_AP_EL1_RW | PTE_SH_INNER | PTE_ATTR_NC)

#define PAGE_SYS          (PTE_AP_EL1_RW | PTE_SH_INNER | PTE_ATTR_NORMAL)
#define PAGE_USR          (PTE_AP_EL1_RW | PTE_SH_INNER | PTE_ATTR_NORMAL)
#define PAGE_DEV          (PTE_AP_EL1_RW | PTE_SH_OUTER | PTE_ATTR_DEVICE | PTE_PXN | PTE_UXN)

// TCR_EL1 Definitions
#define TCR_T0SZ(x)       ((64UL - (x)) & 0x3F)
#define TCR_T1SZ(x)       (((64UL - (x)) & 0x3F) << 16)
#define TCR_IRGN0_WBWA    (1UL << 8)
#define TCR_ORGN0_WBWA    (1UL << 10)
#define TCR_SH0_INNER     (3UL << 12)
#define TCR_TG0_4KB       (0UL << 14)
#define TCR_TG1_4KB       (2UL << 30)
#define TCR_IPS_40BIT     (2UL << 32)

typedef u64 page_dir_t;

// armv7-a compatibility macros
#define L1_PAGE_TABLE     (PTE_VALID | PTE_TYPE_TABLE)
#define L2_SMALL_PAGE     (PTE_VALID | PTE_TYPE_PAGE | PTE_AF)

u64* page_create(u32 level);
void page_map_on(u64* pgd, u64 vaddr, u64 paddr, u64 flags);
void page_unmap_on(u64* pgd, u64 vaddr);
void* page_v2p(u64* pgd, void* vaddr);
void page_copy(u64* old_pgd, u64* new_pgd);
u64* page_clone(u64* old_pgd, u32 level);
void mm_page_enable(u64 page_dir);

static inline u64 pgd_index(u64 addr) { return (addr >> PGD_SHIFT) & 0x1FF; }
static inline u64 pmd_index(u64 addr) { return (addr >> PMD_SHIFT) & 0x1FF; }
static inline u64 pte_index(u64 addr) { return (addr >> PTE_SHIFT) & 0x1FF; }

#endif
