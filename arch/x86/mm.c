/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "../cpu.h"
#include "../pmemory.h"
#include "cpu.h"

extern boot_info_t* boot_info;
extern memory_manager_t mmt;

void* page_v2p(u64* page_dir_ptr_tab, void* vaddr) {
  u32 pdpte_index = (u32)vaddr >> 30 & 0x03;
  u32 pde_index = (u32)vaddr >> 21 & 0x01FF;
  u32 pte_index = (u32)vaddr >> 12 & 0x01FF;
  u32 offset = (u32)vaddr & 0x0FFF;
  u64* page_dir_ptr = (u64)page_dir_ptr_tab[pdpte_index] & ~0xFFF;
  if (page_dir_ptr == NULL) {
    // kprintf("page dir find errro\n");
    return NULL;
  }
  u64* page_tab_ptr = (u64)page_dir_ptr[pde_index] & ~0xFFF;
  if (page_tab_ptr == NULL) {
    // kprintf("page tab find errro\n");
    return NULL;
  }
  void* phyaddr = page_tab_ptr[pte_index] & ~0xFFF;
  // kprintf("page_v2p %x\n",phyaddr);
  if (phyaddr == NULL) {
    return NULL;
  }
  return phyaddr + offset;
}

u32* page_create(u32 level) {
  u32* page_dir_ptr_tab = mm_alloc_zero_align(sizeof(u64) * 4, 0x1000);
  return page_dir_ptr_tab;
}

// 回收
void page_free(u32* old_page, u32 level) {
  if (old_page == NULL) return;
  if (level == USER_MODE) {
    // u64* page = old_page;
    // for (int pdpte_index = 0; pdpte_index < 4; pdpte_index++) {
    //   u64* page_dir_ptr = page[pdpte_index] & ~0xFFF;
    //   if (page_dir_ptr != NULL) {
    //     kprintf("pdpte_index---->%d\n", pdpte_index);
    //     for (int pde_index = 0; pde_index < 512; pde_index++) {
    //       u64* page_tab_ptr = (u64)page_dir_ptr[pde_index] & ~0xFFF;
    //       if (page_tab_ptr != NULL) {
    //         kprintf("pdpte_index---->%d pde_index-> %d\n", pdpte_index,
    //                 pde_index);
    //         for (int pte_index = 0; pte_index < 512; pte_index++) {
    //           u64* page_tab_item = page_tab_ptr[pte_index] & ~0xFFF;
    //           // page_tab_ptr[pte_index]=0;
    //         }
    //       }
    //     }
    //     page[pdpte_index]=0;
    //   }
    // }
  }
}

void page_copy(u32* old_page, u32* new_page) {
  u64* page = old_page;
  if (old_page == NULL) {
    kprintf("page clone old page is null\n");
    return;
  }
  u64* page_dir_ptr_tab = new_page;
  for (int pdpte_index = 0; pdpte_index < 4; pdpte_index++) {
    u64* page_dir_ptr = page[pdpte_index] & ~0xFFF;
    if (page_dir_ptr != NULL) {
      // kprintf("pdpte_index---->%d\n", pdpte_index);
      u64* new_page_dir_ptr = mm_alloc_zero_align(sizeof(u64) * 512, PAGE_SIZE);
      page_dir_ptr_tab[pdpte_index] =
          ((u64)new_page_dir_ptr) | PAGE_P | PAGE_USU | PAGE_RWW;
      for (int pde_index = 0; pde_index < 512; pde_index++) {
        u64* page_tab_ptr = (u64)page_dir_ptr[pde_index] & ~0xFFF;
        if (page_tab_ptr != NULL) {
          // kprintf("pdpte_index---->%d pde_index-> %d\n", pdpte_index,
          //         pde_index);
          u64* new_page_tab_ptr =
              mm_alloc_zero_align(sizeof(u64) * 512, PAGE_SIZE);
          new_page_dir_ptr[pde_index] =
              ((u64)new_page_tab_ptr) | PAGE_P | PAGE_USU | PAGE_RWW;
          for (int pte_index = 0; pte_index < 512; pte_index++) {
            // kprintf("pdpte_index---->%d pde_index-> %d pte_index %d\n",
            // pdpte_index, pde_index,pte_index);

            u64* page_tab_item = page_tab_ptr[pte_index] & ~0xFFF;
            if (page_tab_item != NULL) {
              new_page_tab_ptr[pte_index] = page_tab_ptr[pte_index];
              u32 virtualaddr = (pdpte_index & 0x03) << 30 |
                                (pde_index & 0x01FF) << 21 |
                                (pte_index & 0x01FF) << 12;
              // kprintf("map vir:%x phy:%x \n",
              // virtualaddr,new_page_tab_ptr[pte_index]& ~0xFFF);
            }
          }
        }
      }
    }
  }
}

// u32* page_clone(u32* old_page_dir, u32 level) {
//   if (level == KERNEL_MODE) {
//     return kernel_page_dir_ptr_tab;
//   }
//   if (level == USER_MODE) {
//     u32* page_dir_ptr_tab = mm_alloc_zero_align(sizeof(u64) * 4, 0x1000);
//     for (int i = 0; i < 4; i++) {
//       page_dir_ptr_tab[i] = 0;
//     }
//     if (old_page_dir == NULL) {
//       old_page_dir = kernel_page_dir_ptr_tab;
//     }
//     page_copy(old_page_dir, page_dir_ptr_tab);
//     return page_dir_ptr_tab;
//   }
//   if (level == -1) {
//     u32* page_dir_ptr_tab = mm_alloc_zero_align(sizeof(u64) * 4, 0x1000);
//     page_copy(old_page_dir, page_dir_ptr_tab);
//     return page_dir_ptr_tab;
//   }
//   if (level == -2) {
//     u32* page_dir_ptr_tab = mm_alloc_zero_align(sizeof(u64) * 4, 0x1000);
//     return page_dir_ptr_tab;
//   }
//   return NULL;
// }

u32* page_clone(u32* old_page_dir, u32 level) {
  u32* page_dir_ptr_tab = page_create(level);
  page_copy(old_page_dir, page_dir_ptr_tab);
  return page_dir_ptr_tab;
}

void page_map_on(page_dir_t* page, u32 virtualaddr, u32 physaddr, u32 flags) {
  u32 l1_pdpte_index = (u32)virtualaddr >> 30 & 0x03;
  u32 l2_pde_index = (u32)virtualaddr >> 21 & 0x01FF;
  u32 l3_pte_index = (u32)virtualaddr >> 12 & 0x01FF;
  u32 offset = (u32)virtualaddr & 0x0FFF;
  // kprintf("l1_pdpte_index=>%d l2_pde_index=%d  l3_pte_index=%d\n",
  //         l1_pdpte_index, l2_pde_index, l3_pte_index);

  u64* l1_page_dir_ptr_tab = page;
  u64* l2_page_dir_ptr = (u64)l1_page_dir_ptr_tab[l1_pdpte_index] & ~0xFFF;
  if (l2_page_dir_ptr == NULL) {
    l2_page_dir_ptr = mm_alloc_zero_align(sizeof(u64) * 512, 0x1000);
    l1_page_dir_ptr_tab[l1_pdpte_index] = ((u64)l2_page_dir_ptr) | PAGE_P;
  }
  u64* l3_page_tab_ptr = l2_page_dir_ptr[l2_pde_index] & ~0xFFF;
  if (l3_page_tab_ptr == NULL) {
    l3_page_tab_ptr = mm_alloc_zero_align(sizeof(u64) * 512, 0x1000);
    l2_page_dir_ptr[l2_pde_index] = (u64)l3_page_tab_ptr | flags;
  }
  l3_page_tab_ptr[l3_pte_index] = (u64)physaddr & ~0xFFF | flags;
  // page_tab_ptr[offset] = (u32)physaddr | (flags & 0xFFF);

  // kprintf("map page:%x on vaddr:%x paddr:%x\n",page,virtualaddr,physaddr);
}

void unpage_map_on(page_dir_t* page, u32 virtualaddr) {
  u32 pdpte_index = (u32)virtualaddr >> 30 & 0x03;
  u32 pde_index = (u32)virtualaddr >> 21 & 0x01FF;
  u32 pte_index = (u32)virtualaddr >> 12 & 0x01FF;
  u32 offset = (u32)virtualaddr & 0x0FFF;
  // kprintf("pdpte_index=>%d pde_index=%d  pte_index=%d\n", pdpte_index,
  //         pde_index, pte_index);

  u64* page_dir_ptr = (u64)page[pdpte_index] & ~0xFFF;
  if (page_dir_ptr != NULL) {
    page[pdpte_index] = 0;
  }
  u64* page_tab_ptr = (u64)page_dir_ptr[pde_index] & ~0xFFF;
  if (page_tab_ptr != NULL) {
    page_dir_ptr[pde_index] = 0;
  }
  if (page_tab_ptr != NULL) {
    page_tab_ptr[pte_index] = 0;
  }
  if (page_dir_ptr != NULL) {
    mm_free_align(page_dir_ptr);
  }
  if (page_tab_ptr != NULL) {
    mm_free_align(page_tab_ptr);
  }
}

void mm_init_default(u32 kernel_page_dir) {
  boot_info->pdt_base = kernel_page_dir;
  boot_info->page_type = 1;  // pae mode

  // unsigned int address = 0;
  // // map mem block 100 page 20000k 2m
  // map_mem_block(PAGE_SIZE * 10000 * 2, PAGE_P | PAGE_USU | PAGE_RWW);

  // // map 0 - 0x14000
  // page_map_range(0, 0, PAGE_SIZE * 200, PAGE_P | PAGE_USU | PAGE_RWW);

  // map_kernel(PAGE_P | PAGE_USU | PAGE_RWW,PAGE_P | PAGE_USU | PAGE_RWW );

  page_map(boot_info->kernel_stack, boot_info->kernel_stack,
           PAGE_P | PAGE_USU | PAGE_RWW);

  page_map(boot_info->pdt_base, boot_info->pdt_base,
           PAGE_P | PAGE_USU | PAGE_RWW);
  page_map(boot_info->gdt_base, boot_info->gdt_base,
           PAGE_P | PAGE_USU | PAGE_RWW);

  page_map_range(kernel_page_dir,boot_info->disply.video, boot_info->disply.video,
            boot_info->disply.height * boot_info->disply.width * 2,
            PAGE_P | PAGE_USU | PAGE_RWW);
}

void mm_page_enable(u32 page_dir) {
    cpu_enable_paging_pae(page_dir);
    kprintf("paging pae scucess\n");
}
