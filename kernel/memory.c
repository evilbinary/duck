/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "memory.h"

#include "algorithm/queue_pool.h"
#include "thread.h"

queue_pool_t* kernel_pool;
queue_pool_t* user_pool;

lock_t memory_lock;
memory_t memory_summary;

void memory_init() {
  lock_init(&memory_lock);
  memory_summary.total = mm_get_total();
  memory_summary.free = mm_get_free();
  memory_summary.kernel_used = 0;
  memory_summary.user_used = 0;
  kpool_init();
}

memory_t* memory_info() {
  memory_summary.free = memory_summary.total - memory_summary.kernel_used -
                        memory_summary.user_used;
  return &memory_summary;
}

void* phy_alloc(size_t size) {
  if (size == 0) return NULL;
  void* addr = NULL;
  addr = mm_alloc(size);
  memory_static(size, MEMORY_TYPE_USE);
  return addr;
}

void* phy_alloc_aligment(size_t size, int alignment) {
  if (size == 0) return NULL;
  void* addr = NULL;
  addr = mm_alloc_zero_align(size, alignment);
  memory_static(size, MEMORY_TYPE_USE);
  return addr;
}

void* vm_alloc(size_t size) {
  if (size == 0) return NULL;
  void* addr = NULL;
  size = ALIGN(size, MEMORY_ALIGMENT);
  thread_t* current = thread_current();
  if (current == NULL) {
    //内核启动没有进程，使用内核物理内存
    addr = phy_alloc(size);
    return addr;
  }
  addr = current->vmm->alloc_addr;
  current->vmm->alloc_addr += size;
  current->vmm->alloc_size += size;

  // log_debug("vm alloc page:%x size:%d addr:%x\n", current->context.upage,
  //           size, addr);
  return addr;
}

void* vm_alloc_alignment(size_t size, int alignment) {
  if (size == 0) return NULL;
  size = ALIGN(size, MEMORY_ALIGMENT);
  void* addr = NULL;
  thread_t* current = thread_current();
  if (current == NULL) {
    //内核启动没有进程，使用内核物理内存
    addr = phy_alloc_aligment(size, alignment);
    return addr;
  }
  addr = current->vmm->alloc_addr;
  // u32 page_alignt = alignment - 1;
  // void* new_addr = ((u32)addr+ alignment) & (~page_alignt) ;
  // void* new_addr = ALIGN( ((u32)addr +alignment), alignment);
  // void* new_addr=addr + alignment;
  // new_addr= ALIGN((u32)new_addr, alignment);

  int offset = alignment - 1 + sizeof(void*);
  void* new_addr = (void**)(((size_t)(addr) + offset) & ~(alignment - 1));
  int new_size = new_addr - addr + size;

  current->vmm->alloc_size += new_size;
  current->vmm->alloc_addr += new_size;

  // log_debug("vm alloc a page:%x size:%d addr:%x\n", current->context.upage,
  //           new_size, new_addr);

  return new_addr;
}

void vm_free(void* ptr) {
  thread_t* current = thread_current();
  void* addr = kvirtual_to_physic(ptr, 0);
  size_t size = mm_get_size(addr);
  mm_free(addr);
  memory_static(size, MEMORY_TYPE_FREE);
}

void vm_free_alignment(void* ptr) {
  thread_t* current = thread_current();
  void* addr = kvirtual_to_physic(ptr, 0);
  size_t size = mm_get_size(addr);
  mm_free_align(addr);
  memory_static(size, MEMORY_TYPE_FREE);
}

#ifdef MALLOC_TRACE

int alloc_count = 0;
int alloc_total = 0;
int free_count = 0;
int free_total = 0;

void* kmalloc_trace(size_t size, u32 flag, void* name, void* no, void* fun) {
  void* addr = NULL;
  if (flag & KERNEL_TYPE || flag & DEVICE_TYPE) {
    addr = phy_alloc(size);
  } else {
    addr = vm_alloc(size);
  }
  alloc_total += size;
  void* paddr = kvirtual_to_physic(addr, 0);
  log_debug(
      "kmalloc count:%04d total:%06dk size:%04d addr:%06x paddr:%06x %s:%d "
      "%s\n",
      alloc_count++, alloc_total / 1024, size, addr, paddr, name, no, fun);
  if (addr == NULL) {
    log_error("kmalloc error\n");
    return addr;
  }
  return addr;
}

void* kmalloc_alignment_trace(size_t size, int alignment, u32 flag, void* name,
                              void* no, void* fun) {
  void* addr = NULL;
  if (flag & KERNEL_TYPE || flag & DEVICE_TYPE) {
    addr = phy_alloc_aligment(size, alignment);
  } else {
    addr = vm_alloc_alignment(size, alignment);
  }
  alloc_total += size;
  void* paddr = kvirtual_to_physic(addr, 0);

  log_debug(
      "kmalloca count:%04d total:%06dk size:%04d addr:%06x paddr:%06x %s:%d "
      "%s\n",
      alloc_count++, alloc_total / 1024, size, addr, paddr, name, no, fun);
  return addr;
}

void kfree_trace(void* ptr, void* name, void* no, void* fun) {
  size_t size = mm_get_size(ptr);
  u32 tid = -1;
  log_debug("kfree count:%d total:%dk size:%d addr:%x %s:%d %s\n", free_count++,
            free_total / 1024, size, ptr, name, no, fun);

  vm_free(ptr);
  // memory_static(size, MEMORY_TYPE_FREE);
}

void kfree_alignment_trace(void* ptr, void* name, void* no, void* fun) {
  size_t size = mm_get_size(ptr);
  log_debug("kfreea count:%d total:%dk size:%d addr:%x %s:%d %s\n",
            free_count++, free_total / 1024, size, ptr, name, no, fun);

  vm_free_alignment(ptr);
  // memory_static(size, MEMORY_TYPE_FREE);
}

#else

void* kmalloc(size_t size, u32 flag) {
  void* addr = NULL;
  if (flag & KERNEL_TYPE || flag & DEVICE_TYPE) {
    addr = phy_alloc(size);
  } else {
    addr = vm_alloc(size);
  }
  return addr;
}

void* kmalloc_alignment(size_t size, int alignment, u32 flag) {
  void* addr = NULL;
  if (flag & KERNEL_TYPE || flag & DEVICE_TYPE) {
    addr = phy_alloc_aligment(size);
  } else {
    addr = vm_alloc_alignment(size, alignment);
  }
  return addr;
}

void kfree(void* ptr) { vm_free(ptr); }

void kfree_alignment(void* ptr) {
  vm_free_alignment(ptr);
  // size_t size = mm_get_size(ptr);
  // memory_static(size, MEMORY_TYPE_FREE);
}

#endif

void memory_static(u32 size, int type) {
  thread_t* current = thread_current();
  int op = 1;
  if (type == MEMORY_TYPE_FREE) {
    op = -1;
  }
  if (current != NULL) {
    if (current->level == USER_MODE) {
      memory_summary.user_used += size * op;
      current->umem += size * op;
    } else {
      memory_summary.kernel_used += size * op;
      current->kmem += size * op;
    }
  } else {
    if (type == MEMORY_TYPE_USE) {
      memory_summary.kernel_used += size*op;
    } else {
      memory_summary.kernel_used -= size*op;
    }
  }
}

// alloc physic right now on virtual
void* valloc(void* addr, size_t size) {
  thread_t* current = thread_current();
  if (size < PAGE_SIZE) {
    size = PAGE_SIZE;
  }
  if ((size % PAGE_SIZE) > 0) {
    size += PAGE_SIZE;
  }
  u32 page_alignt = PAGE_SIZE - 1;
  void* vaddr = (u32)addr & (~page_alignt);
  // void* vaddr = ALIGN((u32)addr, PAGE_SIZE);

#ifdef USE_POOL
  void* phy_addr = queue_pool_poll(user_pool);
  if (phy_addr == NULL) {
    phy_addr = phy_alloc_aligment(size, PAGE_SIZE);
  } else {
    log_info("use pool addr %x\n", phy_addr);
  }
#else
  void* phy_addr = phy_alloc_aligment(size, PAGE_SIZE);
#endif
  void* paddr = phy_addr;
  for (int i = 0; i < size / PAGE_SIZE; i++) {
#ifdef DEBUG
    log_debug("map page:%x vaddr:%x paddr:%x\n", current->context.upage, vaddr,
              paddr);
#endif
    if (current != NULL) {
      map_page_on(current->context.upage, vaddr, paddr,
                  PAGE_P | PAGE_USU | PAGE_RWW);
    } else {
      map_page(vaddr, paddr, PAGE_P | PAGE_USU | PAGE_RWW);
    }
    // kprintf("vmap vaddr:%x paddr:%x\n", vaddr, paddr);
    vaddr += PAGE_SIZE;
    paddr += PAGE_SIZE;
  }
  return addr;
}

// free
void vfree(void* addr) {
  if (addr == NULL) return;
  thread_t* current = thread_current();
  void* phy = virtual_to_physic(current->context.upage, addr);
  // kprintf("vfree vaddr:%x paddr:%x\n");
  // unmap_page_on(current->context.upage, addr);
  if (phy != NULL) {
#ifdef USE_POOL
    int ret = queue_pool_put(user_pool, phy);
    if (ret == 0) {
      kfree(phy);
    }
#else
    kfree(phy);
#endif
  }
}

void* kvirtual_to_physic(void* addr, int size) {
  thread_t* current = thread_current();
  void* phy = NULL;
  if (current != NULL) {
    phy = virtual_to_physic(current->context.upage, addr);
    if (phy == NULL) {
      log_error("get phy null\n");
      if (size > 0) {
        kmemset(addr, 0, size);
      }
      phy = virtual_to_physic(current->context.upage, addr);
    }
  } else {
    phy = addr;
  }
  return phy;
}

void map_2gb(u64* page_dir_ptr_tab, u32 attr) {
  u32 addr = 0;
  for (int i = 0; i < 0x200000 / PAGE_SIZE; i++) {
    map_page_on(page_dir_ptr_tab, addr, addr, attr);
    addr += PAGE_SIZE;
  }
}

void map_alignment(void* page, void* vaddr, void* buf, u32 size) {
  u32 file_4k = PAGE_SIZE;
  if (size > PAGE_SIZE) {
    file_4k = size;
  }
  for (int i = 0; i < file_4k / PAGE_SIZE; i++) {
    log_debug("map_alignment vaddr:%x paddr:%x\n", vaddr, buf);
    map_page_on(page, (u32)vaddr, (u32)buf, PAGE_P | PAGE_USU | PAGE_RWW);
    vaddr += PAGE_SIZE;
    buf += PAGE_SIZE;
  }
}

void page_clone_user(u64* page, u64* page_dir_ptr_tab) {
  use_kernel_page();
  page_clone(page, page_dir_ptr_tab);
  // unmap_mem_block(page_dir_ptr_tab, 0x200000);
  use_user_page();
}

void kpool_init() {
#ifdef USE_POOL
  kernel_pool = queue_pool_create(KERNEL_POOL_NUM, PAGE_SIZE);
  user_pool = queue_pool_create_align(USER_POOL_NUM, PAGE_SIZE, PAGE_SIZE);
#else
  kernel_pool = NULL;
  user_pool = NULL;
#endif
}

int kpool_put(void* e) { return queue_pool_put(kernel_pool, e); }

void* kpool_poll() {
  void* e = queue_pool_poll(kernel_pool);
  if (e == NULL) {
    log_error("kpool poll is null\n");
  }
  return e;
}

void use_kernel_page() {
  context_t* context = thread_current_context();
  if (context != NULL && cpu_cpl() == KERNEL_MODE) {
    context->tss->cr3 = context->kpage;
    context_switch_page(context->kpage);
  }
}

void use_user_page() {
  context_t* context = thread_current_context();
  if (context != NULL && cpu_cpl() == KERNEL_MODE) {
    context->tss->cr3 = context->upage;
    context_switch_page(context->upage);
  }
}
