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

void check_addr(void* addr) {
  if (addr == NULL) {
    log_error("malloc error addr return null\n");
    cpu_halt();
  }
}

void* phy_alloc(size_t size) {
  if (size == 0) return NULL;
  void* addr = NULL;
  addr = mm_alloc(size);
  check_addr(addr);
  memory_static(size, MEMORY_TYPE_USE);
  return addr;
}

void* phy_alloc_aligment(size_t size, int alignment) {
  if (size == 0) return NULL;
  void* addr = NULL;
  addr = mm_alloc_zero_align(size, alignment);
  check_addr(addr);
  memory_static(size, MEMORY_TYPE_USE);
  return addr;
}

void* vm_alloc(size_t size) {
  if (size == 0) return NULL;
  void* addr = NULL;
  size = ALIGN(size, MEMORY_ALIGMENT);
  thread_t* current = thread_current();
  if (current == NULL) {
    // 内核启动没有进程，使用内核物理内存
    addr = phy_alloc(size);
    check_addr(addr);
    return addr;
  }
  addr = current->vm->vma->alloc_addr;
  current->vm->vma->alloc_addr += size;
  current->vm->vma->alloc_size += size;

  check_addr(addr);

  // log_debug("vm alloc page:%x size:%d addr:%x\n", current->vm->upage,
  //           size, addr);
  return addr;
}

void* vm_alloc_alignment(size_t size, int alignment) {
  if (size == 0) return NULL;
  size = ALIGN(size, MEMORY_ALIGMENT);
  void* addr = NULL;
  thread_t* current = thread_current();
  if (current == NULL) {
    // 内核启动没有进程，使用内核物理内存
    addr = phy_alloc_aligment(size, alignment);
    check_addr(addr);
    return addr;
  }
  addr = current->vm->vma->alloc_addr;
  // u32 page_alignt = alignment - 1;
  // void* new_addr = ((u32)addr+ alignment) & (~page_alignt) ;
  // void* new_addr = ALIGN( ((u32)addr +alignment), alignment);
  // void* new_addr=addr + alignment;
  // new_addr= ALIGN((u32)new_addr, alignment);

  int offset = alignment - 1 + sizeof(void*);
  void* new_addr = (void**)(((size_t)(addr) + offset) & ~(alignment - 1));
  int new_size = new_addr - addr + size;

  current->vm->vma->alloc_size += new_size;
  current->vm->vma->alloc_addr += new_size;

  // log_debug("vm alloc a page:%x size:%d addr:%x\n", current->vm->upage,
  //           new_size, new_addr);
  check_addr(new_addr);

  return new_addr;
}

void vm_free(void* ptr) {
  thread_t* current = thread_current();
  void* addr = kpage_v2p(ptr, 0);
  kassert(addr!=NULL);
  size_t size = mm_get_size(addr);
  mm_free(addr);
  memory_static(size, MEMORY_TYPE_FREE);
}

void vm_free_alignment(void* ptr) {
  thread_t* current = thread_current();
  void* addr = kpage_v2p(ptr, 0);
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
  void* paddr = kpage_v2p(addr, 0);
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
  void* paddr = kpage_v2p(addr, 0);

  log_debug(
      "kmalloca count:%04d total:%06dk size:%04d addr:%06x paddr:%06x %s:%d "
      "%s\n",
      alloc_count++, alloc_total / 1024, size, addr, paddr, name, no, fun);
  return addr;
}

void kfree_trace(void* ptr, void* name, void* no, void* fun) {
  size_t size = mm_get_size(ptr);
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
#ifdef VM_ENABLE
    addr = vm_alloc(size);
#else
    addr = phy_alloc(size);
#endif
  }
  return addr;
}

void* kmalloc_alignment(size_t size, int alignment, u32 flag) {
  void* addr = NULL;
  if (flag & KERNEL_TYPE || flag & DEVICE_TYPE) {
    addr = phy_alloc_aligment(size, alignment);
  } else {
#ifdef VM_ENABLE
    addr = vm_alloc_alignment(size, alignment);
#else
    addr = phy_alloc_aligment(size, alignment);
#endif
  }

  return addr;
}

void kfree(void* ptr) { vm_free(ptr); }

void kfree_alignment(void* ptr) {
  vm_free_alignment(ptr);
  // size_t size = mm_get_size(ptr);
  // memory_static(size, MEMORY_TYPE_FREE);
}

int kmem_size(void* ptr) {
  if (ptr == NULL) return 0;
  size_t size = mm_get_size(ptr);
  return size;
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
      current->mem += size * op;
    } else {
      memory_summary.kernel_used += size * op;
      current->mem += size * op;
    }
  } else {
    if (type == MEMORY_TYPE_USE) {
      memory_summary.kernel_used += size * op;
    } else {
      memory_summary.kernel_used -= size * op;
    }
  }
}

// #define DEBUG

void* extend_stack(void* addr, size_t size) {
  thread_t* current = thread_current();
  u32 page_alignt = PAGE_SIZE - 1;
  void* vaddr = (u32)addr & (~page_alignt);
  void* aaddr = valloc(addr, size);

  vmemory_area_t* vm = vmemory_area_find_flag(current->vm->vma, MEMORY_STACK);

  if (vm->alloc_addr < addr) {
    u32 alignment = PAGE_SIZE;

    int offset = alignment - 1 + sizeof(void*);
    void* new_addr = (void**)(((size_t)(addr) + offset) & ~(alignment - 1));
    int new_size = new_addr - addr + size;
    vm->alloc_size += new_size;
    vm->alloc_addr -= new_size;
  }
  return aaddr;
}

// alloc physic right now on virtual,use for heap
void* valloc(void* addr, size_t size) {
  thread_t* current = thread_current();
  u32 page_alignt = PAGE_SIZE - 1;
  void* vaddr = (u32)addr & (~page_alignt);
  // void* vaddr = ALIGN((u32)addr, PAGE_SIZE);

  u32 pages = (size / PAGE_SIZE) + (size % PAGE_SIZE == 0 ? 0 : 1);
  for (int i = 0; i < pages; i++) {
#ifdef USE_POOL
    void* phy_addr = queue_pool_poll(user_pool);
    if (phy_addr == NULL) {
      phy_addr = kmalloc_alignment(PAGE_SIZE, PAGE_SIZE, KERNEL_TYPE);
    } else {
      log_info("use pool addr %x\n", phy_addr);
    }
#else
    void* phy_addr = kmalloc_alignment(PAGE_SIZE, PAGE_SIZE, KERNEL_TYPE);
#endif
    void* paddr = phy_addr;
#ifdef DEBUG
    log_debug("map page:%x vaddr:%x paddr:%x\n", current->vm->upage, vaddr,
              paddr);
#endif
    if (current != NULL) {
      page_map_on(current->vm->upage, vaddr, paddr,
                  PAGE_P | PAGE_USR | PAGE_RWX);
    } else {
      page_map(vaddr, paddr, PAGE_P | PAGE_USR | PAGE_RWX);
    }
    // kprintf("vmap vaddr:%x paddr:%x\n", vaddr, paddr);
    vaddr += PAGE_SIZE;
  }
  return addr;
}

// free
void vfree(void* addr, size_t size) {
  if (addr == NULL) return;
  thread_t* current = thread_current();

  u32 page_alignt = PAGE_SIZE - 1;
  void* vaddr = (u32)addr & (~page_alignt);

  u32 pages = (size / PAGE_SIZE) + (size % PAGE_SIZE == 0 ? 0 : 1);
  for (int i = 0; i < pages; i++) {
    void* phy = page_v2p(current->vm->upage, vaddr);
#ifdef DEBUG
    log_debug("vfree vaddr:%x paddr:%x\n", vaddr, phy);
#endif
    // todo
    //  unpage_map_on(current->vm->upage, vaddr);
    if (phy != NULL) {
#ifdef USE_POOL
      int ret = queue_pool_put(user_pool, phy);
      if (ret == 0) {
        kfree_alignment(phy);
      }
#else
      kfree_alignment(phy);
#endif
    }
    vaddr += PAGE_SIZE;
  }
}

void* kpage_v2p(void* addr, int size) {
  thread_t* current = thread_current();
#ifdef VM_ENABLE
  void* phy = NULL;
  if (current != NULL) {
    u32 page = NULL;
    if (current->vm == NULL) {
      log_error("vm is null\n");
    } else {
      page = current->vm->upage;
    }
    phy = page_v2p(page, addr);
    if (phy == NULL) {
      log_error("get page: %x vaddr %x phy null\n", page, addr);
      if (size > 0) {
        kmemset(addr, 0, size);
      }
      phy = page_v2p(page, addr);
    }
  } else {
    phy = addr;
  }
  return phy;
#else
  return addr;
#endif
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