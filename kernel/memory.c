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

#ifdef MALLOC_TRACE

int alloc_count = 0;
int alloc_total = 0;
int free_count = 0;
int free_total = 0;

void* kmalloc_trace(size_t size, void* name, void* no, void* fun) {
  void* addr = NULL;
  addr = mm_alloc(size);
  u32 tid = -1;
  thread_t* current = thread_current();
  if (current != NULL) {
    tid = current->id;
  }
  alloc_total += size;
  kprintf("tid:%d kmalloc count:%04d total:%06dk size:%04d addr:%06x %s:%d %s\n", tid,
          alloc_count++, alloc_total / 1024, size, addr, name, no, fun);
  if (addr == NULL) {
    kprintf("kmalloc error\n");
    return addr;
  }
  kmemset(addr, 0, size);
  memory_static(size, MEMORY_TYPE_USE);
  return addr;
}

void* kmalloc_alignment_trace(size_t size, int alignment, void* name, void* no,
                              void* fun) {
  void* addr = mm_alloc_zero_align(size, alignment);
  u32 tid = -1;
  thread_t* current = thread_current();
  if (current != NULL) {
    tid = current->id;
  }
  alloc_total += size;
  kprintf(
      "tid:%d kmalloca count:%04d total:%06dk size:%04d addr:%06x %s:%d %s\n",
      tid, alloc_count++, alloc_total / 1024, size, addr, name, no, fun);
  memory_static(size, MEMORY_TYPE_USE);
  return addr;
}

void kfree_trace(void* ptr, void* name, void* no, void* fun) {
  size_t size = mm_get_size(ptr);
  u32 tid = -1;
  thread_t* current = thread_current();
  if (current != NULL) {
    tid = current->id;
  }

  kprintf("tid:%d kfree count:%d total:%dk size:%d addr:%x %s:%d %s\n", tid,
          free_count++, free_total / 1024, size, ptr, name, no, fun);

  mm_free(ptr);
  memory_static(size, MEMORY_TYPE_FREE);
}

void kfree_alignment_trace(void* ptr, void* name, void* no, void* fun) {
  size_t size = mm_get_size(ptr);
  u32 tid = -1;
  thread_t* current = thread_current();
  if (current != NULL) {
    tid = current->id;
  }
  kprintf(
      "tid:%d kfreea count:%d total:%dk size:%d addr:%x %s:%d %s\n",
      tid, free_count++, free_total / 1024, size, ptr, name, no, fun);

  mm_free_align(ptr);
  memory_static(size, MEMORY_TYPE_FREE);
}

#else
void* kmalloc(size_t size) {
  void* addr = NULL;
#ifdef X86
  // size = ((size + PAGE_SIZE) / PAGE_SIZE) * PAGE_SIZE;
  // size = ((size + 1024) / 1024) * 1024;
#endif
  addr = mm_alloc(size);
  if (addr == NULL) {
    kprintf("kmalloc error\n");
    return addr;
  }
  kmemset(addr, 0, size);
  memory_static(size, MEMORY_TYPE_USE);
  return addr;
}

void* kmalloc_alignment(size_t size, int alignment) {
  // size=((size+4096)/4096)*4096;
  void* addr = mm_alloc_zero_align(size, alignment);
  memory_static(size, MEMORY_TYPE_USE);
  return addr;
}

void kfree(void* ptr) {
  size_t size = mm_get_size(ptr);
  mm_free(ptr);
  memory_static(size, MEMORY_TYPE_FREE);
}

void kfree_alignment(void* ptr) {
  size_t size = mm_get_size(ptr);
  mm_free_align(ptr);
  memory_static(size, MEMORY_TYPE_FREE);
}

#endif

void memory_static(u32 size, int type) {
  thread_t* current = thread_current();
  if (current != NULL) {
    if (type == MEMORY_TYPE_USE) {
      current->mem_size += size;
      memory_summary.user_used += size;
    } else {
      current->mem_size -= size;
      memory_summary.user_used -= size;
    }
  } else {
    if (type == MEMORY_TYPE_USE) {
      // kprintf("sub kerenl %lu  + size %d\n", (u32)memory_summary.kernel_used,
              // size);
      memory_summary.kernel_used += size;
    } else {
      // kprintf("sub kerenl %lu  - size %d\n", (u32)memory_summary.kernel_used,
      //         size);
      memory_summary.kernel_used -= size;
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
#ifdef USE_POOL
  void* phy_addr = queue_pool_poll(user_pool);
  if (phy_addr == NULL) {
    phy_addr = kmalloc_alignment(size, PAGE_SIZE);
  } else {
    kprintf("use pool addr %x\n", phy_addr);
  }
#else
  void* phy_addr = kmalloc_alignment(size, PAGE_SIZE);
#endif
  void* paddr = phy_addr;
  for (int i = 0; i < size / PAGE_SIZE; i++) {
    if (current != NULL) {
      map_page_on(current->context.page_dir, vaddr, paddr,
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

// rease
void vfree(void* addr) {
  if (addr == NULL) return;
  thread_t* current = thread_current();
  void* phy = virtual_to_physic(current->context.page_dir, addr);
  // kprintf("vfree vaddr:%x paddr:%x\n");
  // unmap_page_on(current->context.page_dir, addr);
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
    kprintf("map_alignment vaddr:%x paddr:%x\n", vaddr, buf);
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
    kprintf("kpool poll is null\n");
  }
  return e;
}

void use_kernel_page() {
  context_t* context = thread_current_context();
  if (context != NULL && cpu_cpl() == KERNEL_MODE) {
    context->tss->cr3 = context->kernel_page_dir;
    context_switch_page(context->kernel_page_dir);
  }
}

void use_user_page() {
  context_t* context = thread_current_context();
  if (context != NULL && cpu_cpl() == KERNEL_MODE) {
    context->tss->cr3 = context->page_dir;
    context_switch_page(context->page_dir);
  }
}