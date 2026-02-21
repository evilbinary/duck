/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "memory.h"
#include "thread.h"
#include "libs/include/kernel/string.h"

#define log_debug

void vmemory_area_free(vmemory_area_t* area) {
  if (area == NULL) return;
  vaddr_t vaddr = area->vaddr;
  area->flags = MEMORY_FREE;
  log_debug("vmemory area free %lx - %lx\n", vaddr, area->vend);
  vfree((void*)vaddr, area->size);
}

// alloc by page fault
vmemory_area_t* vmemory_area_alloc(vmemory_area_t* areas, void* addr,
                                   vaddr_t size) {
  vmemory_area_t* area = vmemory_area_find(areas, addr, size);
  if (area != NULL) {
    return area;
  }
  area = vmemory_area_create(addr, size, 0);
  vmemory_area_add(areas, area);
  return area;
}

vmemory_area_t* vmemory_area_create(void* addr, vaddr_t size, u8 flags) {
  vmemory_area_t* area = kmalloc(sizeof(vmemory_area_t), KERNEL_TYPE);
  area->size = size;
  area->next = NULL;
  area->vaddr = (vaddr_t)addr;
  area->vend = (vaddr_t)addr + size;
  area->alloc_addr = (vaddr_t)addr;
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
                                  vaddr_t size) {
  vmemory_area_t* p = areas;
  vaddr_t a = (vaddr_t)addr;
  for (; p != NULL; p = p->next) {
    if ((a >= p->vaddr) && ((a + size) <= p->vend)) {
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
    vmemory_area_t* c = vmemory_area_create((void*)p->vaddr, p->size, p->flags);
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

vmemory_area_t* vmemory_create_default(vaddr_t koffset) {
  vmemory_area_t* vmm =
      vmemory_area_create((void*)(HEAP_ADDR + koffset), MEMORY_HEAP_SIZE, MEMORY_HEAP);
  vmemory_area_t* vmexec =
      vmemory_area_create((void*)(EXEC_ADDR + koffset), MEMORY_EXEC_SIZE, MEMORY_EXEC);
  vmemory_area_add(vmm, vmexec);
  vmemory_area_t* stack = vmemory_area_create((void*)(STACK_ADDR + koffset),
                                              MEMORY_STACK_SIZE, MEMORY_STACK);
  vmemory_area_add(vmm, stack);

  extern boot_info_t* boot_info;

  // default kernel info
  for (int i = 0; i < boot_info->segments_number; i++) {
    vaddr_t size = boot_info->segments[i].size;
    vaddr_t address = (vaddr_t)(uintptr_t)boot_info->segments[i].start;
    u32 type = boot_info->segments[i].type;

    vmemory_area_t* vmmk = NULL;
    if (type == 2) {
      vmmk = vmemory_area_create((void*)address, size, MEMORY_HEAP);
    } else {
      vmmk = vmemory_area_create((void*)address, size, MEMORY_EXEC);
    }
    vmemory_area_add(vmm, vmmk);
  }
  // add dev info
  vmemory_area_t* vmmdev =
      vmemory_area_create((void*)0xfb000000, 1024 * 768 * 4, MEMORY_DEV);
  vmemory_area_add(vmm, vmmdev);

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

void vmemory_map_type(void* page_dir, vaddr_t virt_addr, vaddr_t phy_addr, vaddr_t size,
                      u32 type) {
  if (page_dir == NULL) {
    log_error("vm map faild for page_dir is null\n");
    return;
  }
  vaddr_t offset = 0;
  vaddr_t pages = (size / PAGE_SIZE) + (size % PAGE_SIZE == 0 ? 0 : 1);
  for (int i = 0; i < pages; i++) {
    page_map_on((u64*)page_dir, virt_addr + offset, phy_addr + offset, type);
#ifdef DEBUG
    log_debug("-page:%lx map %d vaddr: %lx - paddr: %lx\n", page_dir, i,
              virt_addr + offset, phy_addr + offset);
#endif
    offset += PAGE_SIZE;
  }
}

void vmemory_map(void* page_dir, vaddr_t virt_addr, vaddr_t phy_addr, vaddr_t size) {
  if (page_dir == NULL) {
    log_error("vm map faild for page_dir is null\n");
    return;
  }
  vaddr_t offset = 0;
  vaddr_t pages = (size / PAGE_SIZE) + (size % PAGE_SIZE == 0 ? 0 : 1);
  for (int i = 0; i < pages; i++) {
    page_map_on((u64*)page_dir, virt_addr + offset, phy_addr + offset,
                PAGE_USR_RW);
#ifdef DEBUG
    log_debug("-page:%lx map %d vaddr: %lx - paddr: %lx\n", page_dir, i,
              virt_addr + offset, phy_addr + offset);
#endif
    offset += PAGE_SIZE;
  }
}

void vmemory_init(vmemory_t* vm, u32 level, vaddr_t usp, u32 usp_size, u32 flags) {
  vaddr_t koffset = 0;
  if (level == LEVEL_KERNEL || level == LEVEL_KERNEL_SHARE) {
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
  if (level == LEVEL_KERNEL_SHARE) {
    vm->upage = vm->kpage;
  } else if (level == LEVEL_KERNEL) {
    vm->upage = page_clone((u64*)vm->kpage, KERNEL_MODE);
  } else {
    vm->upage = page_clone((u64*)vm->kpage, level);
  }

  log_debug("tid %d init vm level %d kpage: %lx upage: %lx\n", vm->tid, level,
            vm->kpage, vm->upage);

  // 映射栈
  vmemory_area_t* vm_stack = vmemory_area_find_flag(vm->vma, MEMORY_STACK);
  if (vm_stack != NULL) {
    vm_stack->alloc_addr = vm_stack->vend - usp_size;
    vm_stack->alloc_size += usp_size;
    vmemory_map(vm->upage, vm_stack->alloc_addr, usp, usp_size);
  }

  if (level == LEVEL_KERNEL || level == LEVEL_KERNEL_SHARE) {
    log_debug("tid %d kernel vm init end\n", vm->tid);
  } else if (level == LEVEL_USER) {
    log_debug("tid %d user vm init end\n", vm->tid);
  }
}

void vmemory_copy_data(vmemory_t* vm_copy, vmemory_t* vm_src, u32 type) {
  vmemory_area_t* vm = vmemory_area_find_flag(vm_src->vma, type);
  vmemory_area_t* cvm = vmemory_area_find_flag(vm_copy->vma, type);

  cvm->vaddr = vm->vaddr;
  cvm->vend = vm->vend;
  cvm->size = vm->size;

  vaddr_t addr = 0;
  vaddr_t end_addr = 0;
  char* type_str = NULL;
  if (type == MEMORY_STACK) {
    addr = vm->alloc_addr;
    end_addr = addr + vm->alloc_size;
    if (end_addr > vm->vend) {
      log_warn("tid %d stack copy range overflow: %lx > %lx, correcting\n",
               vm_copy->tid, end_addr, vm->vend);
      end_addr = vm->vend;
    }
    if (addr < vm->vaddr) {
      log_warn("tid %d stack copy range underflow: %lx < %lx, correcting\n",
               vm_copy->tid, addr, vm->vaddr);
      addr = vm->vaddr;
    }
    type_str = "stack";
  } else if (type == MEMORY_HEAP) {
    addr = vm->vaddr;
    end_addr = vm->alloc_addr;
    type_str = "heap";
  }

  log_debug("tid %d vm copy %s range: %lx - %lx size: %d\n", vm_copy->tid,
            type_str, addr, end_addr, vm->alloc_size);

#ifdef VM_ENABLE
  vaddr_t copy_start = addr;
  u32 copied_pages = 0;
  for (; addr < end_addr; addr += PAGE_SIZE) {
    void* phy = page_v2p((u64*)vm_src->upage, (void*)addr);
    if (phy != NULL) {
      void* copy_addr = kmalloc_alignment(PAGE_SIZE, PAGE_SIZE, KERNEL_TYPE);
      kmemmove(copy_addr, (void*)addr, PAGE_SIZE);
      log_debug("-copy vaddr %lx addr %lx to %lx\n", addr, phy, copy_addr);
      vmemory_map(vm_copy->upage, addr, (vaddr_t)copy_addr, PAGE_SIZE);
      copied_pages++;
    } else {
      log_warn("thread %d vm copy data,vaddr %lx phy is null\n", vm_copy->tid,
               addr);
    }
  }
  if (type == MEMORY_STACK) {
    cvm->alloc_addr = copy_start;
    cvm->alloc_size = copied_pages * PAGE_SIZE;
  } else if (type == MEMORY_HEAP) {
    cvm->alloc_addr = copy_start + copied_pages * PAGE_SIZE;
    cvm->alloc_size = copied_pages * PAGE_SIZE;
  }
#endif
}

void vmemory_clone(vmemory_t* vmcopy, vmemory_t* vmthread, u32 flags) {
  log_debug("vm clone init\n");

  vmcopy->vma = vmemory_area_clone(vmthread->vma, 1);
  vmcopy->kpage = page_kernel_dir();
  vmcopy->upage = page_clone((u64*)vmthread->upage, 3);

  // 栈拷贝并映射
  vmemory_copy_data(vmcopy, vmthread, MEMORY_STACK);

  // 堆拷贝并映射
  vmemory_copy_data(vmcopy, vmthread, MEMORY_HEAP);

  // init 0
  vmcopy->vma->alloc_size = 0;

  log_debug("vm clone end\n");
}