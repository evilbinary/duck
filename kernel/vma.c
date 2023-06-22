/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "memory.h"

void vmemory_area_free(vmemory_area_t* area) {
  if (area == NULL) return;
  u32 vaddr = area->vaddr;
  // todo fix me
  area->flags = MEMORY_FREE;
#ifdef DEBUG
  log_debug("vmemory area free %x - %x\n", vaddr, area->vend);
#endif
  vfree(vaddr, area->size);
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
  if (areas == NULL) {
    return NULL;
  }
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

vmemory_area_t* vmemory_area_clone(vmemory_area_t* areas, int flag) {
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

  extern boot_info_t* boot_info;

  // default kernel info
  for (int i = 0; i < boot_info->segments_number; i++) {
    u32 size = boot_info->segments[i].size;
    u32 address = boot_info->segments[i].start;
    u32 type = boot_info->segments[i].type;

    vmemory_area_t* vmmk = NULL;
    if (type == 2) {
      vmmk = vmemory_area_create(address, size, MEMORY_HEAP);
    } else {
      vmmk = vmemory_area_create(address, size, MEMORY_EXEC);
    }
    vmemory_area_add(vmm, vmmk);
  }

  return vmm;
}

void vmemory_dump(vmemory_t* vm) {
  char* type[] = {"free", "use",  "share", "heap",
                  "exec", "data", "stack", "mmap"};
  vmemory_area_t* p = vm->vma;
  for (; p != NULL; p = p->next) {
    log_debug(
        "tid %d vaddr:%x vend:%x size:%x alloc p:%x size:%x flag:%x type:%s\n",
        vm->tid, p->vaddr, p->vend, p->size, p->alloc_addr, p->alloc_size,
        p->flags, type[p->flags]);
  }
}

void vmemory_dump_area(vmemory_area_t* area) {
  char* type[] = {"free", "use",  "share", "heap",
                  "exec", "data", "stack", "mmap"};
  vmemory_area_t* p = area;
  for (; p != NULL; p = p->next) {
    log_debug("vaddr:%x vend:%x size:%x alloc p:%x size:%x flag:%x type:%s\n",
              p->vaddr, p->vend, p->size, p->alloc_addr, p->alloc_size,
              p->flags, type[p->flags]);
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
                PAGE_P | PAGE_RW);
#ifdef DEBUG
    log_debug("-page:%x map %d vaddr: %x - paddr: %x\n", page_dir, i,
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
  if (vm == NULL) {
    log_error("vm is null\n");
    return;
  }
  if (usp <= 0 || usp_size <= 0) {
    log_error("vm usp is error\n");
    return;
  }
  vm->vma = vmemory_create_default(koffset);
  vm->kpage = page_kernel_dir();
  if (level == KERNEL_MODE) {
    vm->upage = vm->kpage;
  } else {
    vm->upage = page_clone(vm->kpage, level);
    // vm->upage = page_create(0);
  }

  log_debug("tid %d init vm level %d kpage: %x upage: %x\n", vm->tid, level,
            vm->kpage, vm->upage);

  // 映射栈
  vmemory_area_t* vm_stack = vmemory_area_find_flag(vm->vma, MEMORY_STACK);
  if (vm_stack != NULL) {
    vm_stack->alloc_addr = vm_stack->vend - usp_size;
    vm_stack->alloc_size += usp_size;
    vmemory_map(vm->upage, vm_stack->alloc_addr, usp - usp_size, usp_size);
  }

  if (level == KERNEL_MODE) {
    log_debug("tid %d kernel vm init end\n", vm->tid);
  } else if (level == USER_MODE) {
    log_debug("tid %d user vm init end\n", vm->tid);
  }
}

void vmemory_copy_data(vmemory_t* vm_copy, vmemory_t* vm_src, u32 type) {
  vmemory_area_t* vm = vmemory_area_find_flag(vm_src->vma, type);
  vmemory_area_t* cvm = vmemory_area_find_flag(vm_copy->vma, type);

  cvm->vaddr = vm->vaddr;
  cvm->vend = vm->vend;
  cvm->size = vm->size;

  u32 addr = NULL;
  u32 end_addr = NULL;
  char* type_str = NULL;
  if (type == MEMORY_STACK) {
    addr = vm->alloc_addr;
    end_addr = addr + vm->alloc_size;
    type_str = "stack";
  } else if (type == MEMORY_HEAP) {
    addr = vm->vaddr;
    end_addr = vm->alloc_addr;
    type_str = "heap";
  }

  log_debug("tid %d vm copy %s range: %x - %x size: %d\n", vm_copy->tid,
            type_str, addr, end_addr, vm->alloc_size);

#ifdef VM_ENABLE
  for (; addr < end_addr; addr += PAGE_SIZE) {
    void* phy = page_v2p(vm_src->upage, addr);
    if (phy != NULL) {
      u32* copy_addr = kmalloc_alignment(PAGE_SIZE, PAGE_SIZE, KERNEL_TYPE);
      kmemmove(copy_addr, addr,
               PAGE_SIZE);  // fix v3s crash copy use current page addr
      log_debug("-copy vaddr %x addr %x to %x\n", addr, phy, copy_addr);
      vmemory_map(vm_copy->upage, addr, copy_addr, PAGE_SIZE);
    } else {
      log_warn("thread %d vm copy data,vaddr %x phy is null\n", vm_copy->tid,
               addr);
    }
  }
#endif
}

void vmemory_clone(vmemory_t* vmcopy, vmemory_t* vmthread, u32 flags) {
  log_debug("vm clone init\n");

  vmcopy->vma = vmemory_area_clone(vmthread->vma, 1);
  vmcopy->kpage = page_kernel_dir();
  // vmcopy->upage = page_clone(vmcopy->kpage, 3);
  vmcopy->upage = page_clone(vmthread->upage, 3);

  // 栈拷贝并映射
  vmemory_copy_data(vmcopy, vmthread, MEMORY_STACK);

  // 堆拷贝并映射
  vmemory_copy_data(vmcopy, vmthread, MEMORY_HEAP);

  // init 0
  vmcopy->vma->alloc_size = 0;

  log_debug("vm clone end\n");
}