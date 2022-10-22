/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "../mm.h"

#include "../cpu.h"
#include "cpu.h"

extern boot_info_t* boot_info;
extern memory_manager_t mmt;

u64 kernel_page_dir_ptr_tab[4] __attribute__((aligned(0x20)));
u64 kernel_page_dir[512] __attribute__((aligned(0x1000)));
u64 kernel_page_tab[512] __attribute__((aligned(0x1000)));

void map_page_on(page_dir_t* page, u32 virtualaddr, u32 physaddr, u32 flags) {
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
}

void map_page(u32 virtualaddr, u32 physaddr, u32 flags) {
  map_page_on(kernel_page_dir_ptr_tab, virtualaddr, physaddr, flags);
}

void unmap_page_on(page_dir_t* page, u32 virtualaddr) {
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

void mm_test() {
  // map_page(0x90000,0x600000,3);
  // 0xfd000000
  // map_page(0x1ff000,0x400000,PAGE_P | PAGE_USU | PAGE_RWW);
  // map_page(0xfd000000, 0xfd000000, PAGE_P | PAGE_USU | PAGE_RWW);

  // u32 addr = 0x100000;
  // for (int i = 0; i < 100; i++) {
  //   map_page(addr, addr, PAGE_P | PAGE_USU | PAGE_RWW);
  //   addr += 0x1000;
  // }
}

void map_mem_block(u32 addr, void* page) {
  mem_block_t* p = mmt.blocks;
  for (; p != NULL; p = p->next) {
    if (p->origin_addr > addr) {
      // map_page(p->addr, p->addr, PAGE_P | PAGE_USU | PAGE_RWW);
      u32 address = p->origin_addr;
      for (int i = 0; i < p->size / 0x1000; i++) {
        map_page_on(page, address, address, PAGE_P | PAGE_USU | PAGE_RWW);
        // kprintf("map addr %x %x\n", address, address);
        address += 0x1000;
      }
    }
  }
}

void unmap_mem_block(u32* page, u32 addr) {
  for (int i = 0; i < boot_info->memory_number; i++) {
    memory_info_t* mem = (memory_info_t*)&boot_info->memory[i];
    if (mem->type != 1) {  // normal ram
      continue;
    }
    if (mem->base > addr) {
      u32 address = mem->base;
      for (int i = 0; i < mem->length / 0x1000; i++) {
        unmap_page_on(page, address);
        kprintf("unmap addr %x %x\n", address, address);
        address += 0x1000;
      }
    }
  }
}

void mm_init_default() {
  boot_info->pdt_base = (ulong)kernel_page_dir_ptr_tab;
  boot_info->page_type = 1;  // pae mode

  // set zero
  for (int i = 0; i < 4; i++) {
    kernel_page_dir_ptr_tab[i] = 0;
  }
  for (int i = 0; i < 512; i++) {
    kernel_page_dir[i] = 0;
    kernel_page_tab[i] = 0;
  }
  kernel_page_dir_ptr_tab[0] = (u32)kernel_page_dir | PAGE_P;
  kernel_page_dir[0] = (u32)kernel_page_tab | (PAGE_P | PAGE_USU | PAGE_RWW);

  // // //map 0-0x200000 2GB
  unsigned int i, address = 0;
  kprintf("start 0x%x ", address);
  for (i = 0; i < 512; i++) {
    map_page(address, address, (PAGE_P | PAGE_USU | PAGE_RWW));
    address = address + 0x1000;
  }
  kprintf("- 0x%x\n", address);  // 0x200000 2GB

  address = boot_info->kernel_entry;
  kprintf("map kernel %x ", address);
  for (i = 0; i < (((u32)boot_info->kernel_size) / 0x1000 + 6); i++) {
    map_page(address, address, (PAGE_P | PAGE_USU | PAGE_RWW));
    address += 0x1000;
  }
  kprintf("- 0x%x\n", address);

  // map > 4GB addr
  map_mem_block(0x200000, kernel_page_dir_ptr_tab);

  if (boot_info->pdt_base != NULL) {
    ulong addr = (ulong)boot_info->pdt_base;
    cpu_enable_paging_pae(addr);
    kprintf("paging pae scucess\n");
  }
}

void* virtual_to_physic(u64* page_dir_ptr_tab, void* vaddr) {
  u32 pdpte_index = (u32)vaddr >> 30 & 0x03;
  u32 pde_index = (u32)vaddr >> 21 & 0x01FF;
  u32 pte_index = (u32)vaddr >> 12 & 0x01FF;
  u32 offset = (u32)vaddr & 0x0FFF;
  u64* page_dir_ptr = (u64)page_dir_ptr_tab[pdpte_index] & ~0xFFF;
  if (page_dir_ptr == NULL) {
    kprintf("page dir find errro\n");
    return NULL;
  }
  u64* page_tab_ptr = (u64)page_dir_ptr[pde_index] & ~0xFFF;
  if (page_tab_ptr == NULL) {
    // kprintf("page tab find errro\n");
    return NULL;
  }
  void* phyaddr = page_tab_ptr[pte_index] & ~0xFFF;
  // kprintf("virtual_to_physic %x\n",phyaddr);
  return phyaddr;
}

void page_clone(u32* old_page, u32* new_page) {
  u64* page = old_page;
  u64* page_dir_ptr_tab = new_page;
  for (int pdpte_index = 0; pdpte_index < 4; pdpte_index++) {
    u64* page_dir_ptr = page[pdpte_index] & ~0xFFF;
    if (page_dir_ptr != NULL) {
      // kprintf("pdpte_index---->%d\n", pdpte_index);
      u64* new_page_dir_ptr = kmalloc_alignment(sizeof(u64) * 512, PAGE_SIZE);
      page_dir_ptr_tab[pdpte_index] =
          ((u64)new_page_dir_ptr) | PAGE_P | PAGE_USU | PAGE_RWW;
      for (int pde_index = 0; pde_index < 512; pde_index++) {
        u64* page_tab_ptr = (u64)page_dir_ptr[pde_index] & ~0xFFF;
        if (page_tab_ptr != NULL) {
          // kprintf("pdpte_index---->%d pde_index-> %d\n", pdpte_index,
          //         pde_index);
          u64* new_page_tab_ptr =
              kmalloc_alignment(sizeof(u64) * 512, PAGE_SIZE);
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

u32* page_alloc_clone(u32* old_page_dir, u32 level) {
  if (level == KERNEL_MODE) {
    return old_page_dir;
  }
  if (level == USER_MODE) {
    u32* page_dir_ptr_tab = kmalloc_alignment(sizeof(u64) * 4, 0x1000);

    // map 0-0x200000 2GB
    // unsigned int i, address = 0;
    // kprintf("page alloc clone start 0x%x ", address);
    // for (i = 0; i < 512; i++) {
    //   map_page_on(page_dir_ptr_tab, address, address,
    //               PAGE_P | PAGE_USU | PAGE_RWW);
    //   address = address + 0x1000;
    // }
    // kprintf("- 0x%x\n", address);

    // u32 virtualaddr =
    //     (3 & 0x03) << 30 | (488 & 0x01FF) << 21 | (0 & 0x01FF) << 12;
    // address = virtualaddr;
    // for (i = 0; i < 512; i++) {
    //   map_page_on(page_dir_ptr_tab, address, address,
    //               PAGE_P | PAGE_USU | PAGE_RWW);
    //   address = address + 0x1000;
    // }

    // address = boot_info->kernel_entry;
    // kprintf("map kernel %x ", address);
    // for (i = 0; i < (((u32)boot_info->kernel_size) / 0x1000 + 6); i++) {
    //   map_page_on(page_dir_ptr_tab, address, address,
    //               (PAGE_P | PAGE_USU | PAGE_RWW));
    //   address += 0x1000;
    // }
    // kprintf("- 0x%x\n", address);

    // map_mem_block(0x200000, page_dir_ptr_tab);

    page_clone(old_page_dir, page_dir_ptr_tab);
    return page_dir_ptr_tab;
  }
  if (level == -1) {
    u32* page_dir_ptr_tab = kmalloc_alignment(sizeof(u64) * 4, 0x1000);
    page_clone(old_page_dir, page_dir_ptr_tab);
    return page_dir_ptr_tab;
  }
  return NULL;
}

//回收
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