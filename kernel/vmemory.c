/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "memory.h"

void vmemory_area_free(vmemory_area_t* area) {
  if (area == NULL) return;
  context_t* context = thread_current_context();
  u32 vaddr = area->vaddr;
  for (int i = 0; i < area->size / PAGE_SIZE; i++) {
    kfree_alignment(vaddr);
    map_page_on(context->page_dir, vaddr, vaddr, 0);
    vaddr += PAGE_SIZE;
  }
  area->flags = MEMORY_FREE;
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
  vmemory_area_t* area = kmalloc(sizeof(vmemory_area_t));
  area->size = size;
  area->next = NULL;
  area->vaddr = addr;
  area->vend = addr + size;
  area->alloc_addr = addr;
  area->alloc_size = 0;
  area->flags = flags;
  return area;
}

void vmemory_area_add(vmemory_area_t* areas, vmemory_area_t* area) {
  vmemory_area_t* p = areas;
  for (; p->next != NULL; p = p->next) {
  }
  p->next = area;
}

vmemory_area_t* vmemory_area_find(vmemory_area_t* areas, void* addr,
                                  size_t size) {
  vmemory_area_t* p = areas;
  for (; p != NULL; p = p->next) {
    // kprintf("vmemory_area_find addr: %x p->vaddr:%x
    // p->size:%x\n",addr,p->vaddr,p->size);
    if ((addr >= p->vaddr) && (addr <= (p->vaddr + p->size))) {
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

vmemory_area_t* vmemory_clone(vmemory_area_t* areas) {
  if (areas == NULL) {
    return NULL;
  }
  vmemory_area_t* new_area = NULL;
  vmemory_area_t* current = NULL;
  vmemory_area_t* p = areas;
  for (; p != NULL; p = p->next) {
    vmemory_area_t* c = vmemory_area_create(p->vaddr, p->size, p->flags);
    c->alloc_addr = p->alloc_addr;
    c->alloc_size = p->alloc_size;
    if (new_area == NULL) {
      new_area = c;
      current = c;
    } else {
      current->next = c;
    }
  }
  return new_area;
}