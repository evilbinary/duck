/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "memory.h"

void vmemory_area_free(vmemory_area_t* area) {
  if (area == NULL) return;
  for (; area != NULL; area = area->next) {
    u32 vaddr = area->vaddr;
    // todo fix me
    // vfree(vaddr, area->size);
    area->flags = MEMORY_FREE;
  }
}

// alloc by page fault
vmemory_area_t* vmemory_area_alloc(vmemory_area_t* areas, void* addr,
                                   u32 size) {
  vmemory_area_t* area = vmemory_area_find(areas, addr, size);
  if (area != NULL) {
    return area;
  }
  area = vmemory_area_create(addr, size, 0);
  vmemory_area_add(areas, area);
  return area;
}

vmemory_area_t* vmemory_area_create(void* addr, u32 size, u8 flags) {
  vmemory_area_t* area = kmalloc(sizeof(vmemory_area_t), KERNEL_TYPE);
  area->size = size;
  area->next = NULL;
  area->vaddr = addr;
  area->vend = addr + size;
  area->alloc_addr = addr;
  area->alloc_size = 0;
  area->flags = flags;
  area->child = NULL;
  return area;
}

void vmemory_area_add(vmemory_area_t* areas, vmemory_area_t* area) {
  vmemory_area_t* p = areas;
  for (; p->next != NULL; p = p->next) {
  }
  p->next = area;
}

vmemory_area_t* vmemory_area_find_last(vmemory_area_t* areas) {
  vmemory_area_t* p = areas;
  for (; p->next != NULL; p = p->next) {
  }
  return p;
}

vmemory_area_t* vmemory_area_find(vmemory_area_t* areas, void* addr,
                                  size_t size) {
  vmemory_area_t* p = areas;
  for (; p != NULL; p = p->next) {
    // kprintf("vmemory_area_find addr: %x p->vaddr:%x
    // p->size:%x\n",addr,p->vaddr,p->size);
    if ((addr >= p->vaddr) && ((addr + size) <= p->vend)) {
      return p;
    }
  }
  return NULL;
}

vmemory_area_t* vmemory_area_destroy(vmemory_area_t* area) {}

vmemory_area_t* vmemory_area_find_flag(vmemory_area_t* areas, u32 flags) {
  vmemory_area_t* p = areas;
  for (; p != NULL; p = p->next) {
    if (p->flags == flags) {
      return p;
    }
  }
  return NULL;
}

vmemory_area_t* vmemory_clone(vmemory_area_t* areas, int flag) {
  if (areas == NULL) {
    return NULL;
  }
  vmemory_area_t* new_area = NULL;
  vmemory_area_t* current = NULL;
  vmemory_area_t* p = areas;
  for (; p != NULL; p = p->next) {
    vmemory_area_t* c = vmemory_area_create(p->vaddr, p->size, p->flags);
    if (flag == 1) {
      c->alloc_addr = p->alloc_addr;
      c->alloc_size = p->alloc_size;
    }
    if (new_area == NULL) {
      new_area = c;
      current = c;
    } else {
      current->next = c;
      current = c;
    }
  }
  return new_area;
}

vmemory_area_t* vmemory_create_default(u32 koffset) {
  vmemory_area_t* vmm =
      vmemory_area_create(HEAP_ADDR + koffset, MEMORY_HEAP_SIZE, MEMORY_HEAP);
  vmemory_area_t* vmexec =
      vmemory_area_create(EXEC_ADDR + koffset, MEMORY_EXEC_SIZE, MEMORY_EXEC);
  vmemory_area_add(vmm, vmexec);
  vmemory_area_t* stack = vmemory_area_create(STACK_ADDR + koffset,
                                              MEMORY_STACK_SIZE, MEMORY_STACK);
  vmemory_area_add(vmm, stack);

  // extern boot_info_t* boot_info;

  // default kernel info
  // for (int i = 0; i < boot_info->segments_number; i++) {
  //   u32 size = boot_info->segments[i].size;
  //   u32 address = boot_info->segments[i].start;
  //   u32 type = boot_info->segments[i].type;

  //   vmemory_area_t* vmmk = NULL;
  //   if (type == 2) {
  //     vmmk = vmemory_area_create(address, size, MEMORY_HEAP);
  //   } else {
  //     vmmk = vmemory_area_create(address, size, MEMORY_EXEC);
  //   }
  //   vmemory_area_add(vmm, vmmk);
  // }

  return vmm;
}

void vmemory_dump(vmemory_area_t* areas) {
  vmemory_area_t* p = areas;
  for (; p != NULL; p = p->next) {
    log_debug("vaddr:%x vend:%x size:%x alloc p:%x size:%x flag:%x\n", p->vaddr,
              p->vend, p->size, p->alloc_addr, p->alloc_size, p->flags);
  }
}

#define DEBUG

void vmemory_map(u32* page_dir, u32 virt_addr, u32 phy_addr, u32 size) {
  if (page_dir == NULL) {
    log_error("vm map faild for page_dir is null\n");
    return;
  }
  u32 offset = 0;
  u32 pages = (size / PAGE_SIZE) + (size % PAGE_SIZE == 0 ? 0 : 1);
  for (int i = 0; i < pages; i++) {
    page_map_on(page_dir, virt_addr + offset, phy_addr + offset,
                PAGE_P | PAGE_USU | PAGE_RWW);
#ifdef DEBUG
    log_debug("page:%x map %d vaddr: %x - paddr: %x\n", page_dir, i,
              virt_addr + offset, phy_addr + offset);
#endif
    offset += PAGE_SIZE;
  }
}

void vmemory_init(vmemory_t* vm, u32 level, u32 usp, u32 usp_size, u32 flags) {
  u32 koffset = 0;
  if (level == KERNEL_MODE) {
    koffset += KERNEL_OFFSET;
  }

  vm->vma = vmemory_create_default(koffset);
  vm->upage = page_create(level);
  vm->kpage = page_kernel_dir();

  log_debug("init vm level %d kpage: %x upage: %x\n", level, vm->kpage,
            vm->upage);

  // 映射栈
  vmemory_area_t* vm_stack = vmemory_area_find_flag(vm->vma, MEMORY_STACK);
  if (vm_stack != NULL) {
    vm_stack->alloc_addr = vm_stack->vend - usp_size;
    vm_stack->alloc_size += usp_size;
    vmemory_map(vm->upage, vm_stack->alloc_addr, usp - usp_size, usp_size);
  }

  if (level == KERNEL_MODE) {
    log_debug("kernel vm init end\n");
  } else if (level == USER_MODE) {
    log_debug("user vm init end\n");
  }
}