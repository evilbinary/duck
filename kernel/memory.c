/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "memory.h"

#include "algorithm/queue_pool.h"
#include "rt_mutex.h"
#include "thread.h"

// #define DEBUG 1

queue_pool_t* kernel_pool;
queue_pool_t* user_pool;

/*
 * 方案 A：锁在 kernel，pmemory 保持无锁算法。
 * 所有碰 mmt/freelist 的入口经此锁：phy_alloc / phy_alloc_aligment /
 * vm_free* / valloc / vfree；建 L2 经 kmalloc_alignment → 同一把锁。
 * rt_mutex 可重入：kmalloc → ya_sbrk → page_map → kmalloc_alignment OK。
 */
static rt_mutex_t memory_lock;
memory_t memory_summary;

/* 异常上下文（fault 回溯中，fault_count>0）且 memory_lock 被其他线程
 * 持有时，kmalloc 不能等待（持锁线程可能已被抢占，异常处理不调度，
 * 等待即死锁）：返回失败让调用方放弃，而非自旋 */
static int memory_locked_other(void) {
  thread_t* cur = thread_current();
  if (cur == NULL || cur->fault_count == 0) {
    return 0;
  }
  return memory_lock.owner != NULL && memory_lock.owner != cur;
}

void memory_init() {
  rt_mutex_init(&memory_lock);
  memory_summary.total = mm_get_total();
  memory_summary.free = mm_get_free();
  memory_summary.kernel_used = 0;
  memory_summary.user_used = 0;
  kpool_init();
}

memory_t* memory_info() {
  ulong used = memory_summary.kernel_used + memory_summary.user_used;
  if (used >= memory_summary.total) {
    memory_summary.free = 0;
  } else {
    memory_summary.free = memory_summary.total - used;
  }
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
  if (memory_locked_other()) {
    return NULL;
  }
  void* addr = NULL;
  rt_mutex_lock(&memory_lock);
  addr = mm_alloc(size);
  rt_mutex_unlock(&memory_lock);
  check_addr(addr);
  memory_static(size, MEMORY_TYPE_USE);
  return addr;
}

void* phy_alloc_aligment(size_t size, int alignment) {
  if (size == 0) return NULL;
  if (memory_locked_other()) {
    return NULL;
  }
  void* addr = NULL;
  rt_mutex_lock(&memory_lock);
  addr = mm_alloc_zero_align(size, alignment);
  rt_mutex_unlock(&memory_lock);
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
  if (ptr == NULL || memory_locked_other()) {
    return;
  }
  void* addr = kpage_v2p(ptr, 0);
  kassert(addr != NULL);
  size_t size = 0;
  rt_mutex_lock(&memory_lock);
  size = mm_get_size(addr);
  if (size > 0) {
    mm_free(addr);
  }
  rt_mutex_unlock(&memory_lock);
  if (size == 0) {
    log_error("mfree error %x\n", ptr);
  }
  memory_static(size, MEMORY_TYPE_FREE);
}

void vm_free_alignment(void* ptr) {
  void* addr = kpage_v2p(ptr, 0);
  if (addr <= 0) {
    log_error("vm free aligment error %x\n", ptr);
    return;
  }
  size_t size;
  rt_mutex_lock(&memory_lock);
  size = mm_get_align_size(addr);
  mm_free_align(addr);
  rt_mutex_unlock(&memory_lock);
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
  size_t size = mm_get_align_size(ptr);
  log_debug("kfreea count:%d total:%dk size:%d addr:%x %s:%d %s\n",
            free_count++, free_total / 1024, size, ptr, name, no, fun);

  vm_free_alignment(ptr);
  // memory_static(size, MEMORY_TYPE_FREE);
}

#else

void* kmalloc(size_t size, u32 flag) {
  void* addr = NULL;
#if defined(ARM) && defined(ARMV5)
  // ARM926/ARMv5: kernel often runs under a per-thread TTBR0 (user page table)
  // during syscalls. If a kernel thread allocates via vm_alloc() (0x9xxxxxxx),
  // that virtual mapping will not exist in user page tables and will fault.
  // So: for non-user threads, always allocate from physical memory.
  thread_t* current = thread_current();
  if (current == NULL || current->level != LEVEL_USER) {
    return phy_alloc(size);
  }
#endif
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
#if defined(ARM) && defined(ARMV5)
  thread_t* current = thread_current();
  if (current == NULL || current->level != LEVEL_USER) {
    return phy_alloc_aligment(size, alignment);
  }
#endif
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
  void* addr = kpage_v2p(ptr, 0);
  size_t size = mm_get_align_size(addr);
  vm_free_alignment(ptr);
  memory_static(size, MEMORY_TYPE_FREE);
}

int kmem_size(void* ptr) {
  if (ptr == NULL) return 0;
  size_t size = mm_get_size(ptr);
  return size;
}

#endif

void memory_static(u32 size, int type) {
  thread_t* current = thread_current();
  int is_free = (type == MEMORY_TYPE_FREE);
  if (current != NULL) {
    if (current->level == LEVEL_USER) {
      if (is_free) {
        if (memory_summary.user_used >= size) {
          memory_summary.user_used -= size;
        } else {
          memory_summary.user_used = 0;
        }
        if (current->mem >= size) {
          current->mem -= size;
        } else {
          current->mem = 0;
        }
      } else {
        memory_summary.user_used += size;
        current->mem += size;
      }
    } else {
      if (is_free) {
        if (memory_summary.kernel_used >= size) {
          memory_summary.kernel_used -= size;
        } else {
          memory_summary.kernel_used = 0;
        }
        if (current->mem >= size) {
          current->mem -= size;
        } else {
          current->mem = 0;
        }
      } else {
        memory_summary.kernel_used += size;
        current->mem += size;
      }
    }
  } else {
    if (!is_free) {
      memory_summary.kernel_used += size;
    } else {
      if (memory_summary.kernel_used >= size) {
        memory_summary.kernel_used -= size;
      } else {
        memory_summary.kernel_used = 0;
      }
    }
  }
}

// #define DEBUG

/** 通用栈扩增：确保栈在 sp 向低位还有 >= need 字节已映射。
 *  不足则提前向低分配并映射页、下移 MEMA vma->vaddr。
 *  供 fault 符号化等深调用链进入前调用，避免固定小栈溢出。
 *  返回 0 成功（余量已足够或已扩）; -1 不可扩/失败。 */
int memory_stack_ensure(vmemory_t* vm, u32 sp, u32 need) {
  vmemory_area_t* m;
  u32 base, grow, target, a;

  if (vm == NULL || vm->vma == NULL) {
    return -1;
  }
  m = vmemory_area_find_flag(vm->vma, MEMORY_STACK);
  if (m == NULL) {
    return -1;
  }
  base = (u32)m->vaddr;
  if (sp < base || sp > (u32)m->vend) {
    return -1; /* 当前 sp 不在栈 vma 内：非可扩的用户栈则不处理 */
  }
  if (sp - base >= need) {
    return 0;
  }
  if (memory_locked_other()) {
    return -1;
  }
  grow = need - (sp - base);
  target = (base - grow) & ~(PAGE_SIZE - 1);
  for (a = target; a < base; a += PAGE_SIZE) {
    void* phy = mm_alloc_page();
    if (phy == NULL) {
      return -1;
    }
    kmemset(phy, 0, PAGE_SIZE);
    page_map_on(vm->upage, a, phy, PAGE_P | PAGE_USR | PAGE_RWX);
  }
  m->vaddr = (void*)target;
  return 0;
}

void* extend_stack(void* addr, size_t size) {
  thread_t* current = thread_current();
  u32 page_alignt = PAGE_SIZE - 1;
  void* vaddr = (vaddr_t)addr & (~page_alignt);
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
  void* vaddr = (vaddr_t)addr & (~page_alignt);
  u32 pages = (size / PAGE_SIZE) + (size % PAGE_SIZE == 0 ? 0 : 1);

  for (u32 i = 0; i < pages; i++) {
    rt_mutex_lock(&memory_lock);
    void* phy_addr = mm_alloc_page();
    rt_mutex_unlock(&memory_lock);
    if (phy_addr == NULL) {
      log_error("valloc: mm_alloc_page failed vaddr=%lx\n", vaddr);
      return NULL;
    }
    /* First zero through the kernel identity mapping so the physical page
     * is clean even before we install the user mapping. */
    kmemset(phy_addr, 0, PAGE_SIZE);
    memory_static(PAGE_SIZE, MEMORY_TYPE_USE);
    /* L2 分配走 kmalloc_alignment，与 memory_lock 同一把（可重入） */
    if (current != NULL) {
      page_map_on(current->vm->upage, vaddr, phy_addr,
                  PAGE_P | PAGE_USR | PAGE_RWX);
    } else {
      page_map(vaddr, phy_addr, PAGE_P | PAGE_USR | PAGE_RWX);
    }

    /* Zero the page *again* through the user virtual address after mapping.
     * This guarantees the D-cache is filled with zeros for the user-side VA
     * translation, eliminating any possibility that the user reads stale
     * data from a previous occupant of the same physical page.
     *
     * On Cortex-A7 the D-cache is PIPT, so in theory kmemset(VA=PA)
     * already placed zeros in the right cache lines.  However, on real
     * T113-S3 hardware we still observe p[-4] != 0 in mallocng's enframe.
     * Re-zeroing through the user VA is the safest way to ensure the user
     * will read back zeros — whatever the root cause of the discrepancy
     * (speculative fills, cache maintenance subtlety, or TLB race). */
    kmemset((void*)vaddr, 0, PAGE_SIZE);
    vaddr += PAGE_SIZE;
  }
  return addr;
}

// free
void vfree(void* addr, size_t size) {
  if (addr == NULL) return;
  thread_t* current = thread_current();

  u32 page_alignt = PAGE_SIZE - 1;
  void* vaddr = (vaddr_t)addr & (~page_alignt);

  u32 pages = (size / PAGE_SIZE) + (size % PAGE_SIZE == 0 ? 0 : 1);
  for (int i = 0; i < pages; i++) {
    void* phy = page_v2p(current->vm->upage, vaddr);
    #ifdef DEBUG
    log_debug("vfree vaddr:%x paddr:%x\n", vaddr, phy);
    #endif
    if (phy != NULL) {
#if defined(ARM) || defined(ARMV7_A) || defined(ARMV7) || defined(ARMV5) || \
    defined(__arm__)
      /* Clean + invalidate D-cache for both the user VA and the PA before
       * unmapping, so that stale cache lines do not corrupt the page when
       * it is later reallocated by valloc to a different VA. */
      {
        extern void dccmvac(unsigned long mva);
        unsigned long va;
        for (va = (unsigned long)vaddr;
             va < (unsigned long)vaddr + PAGE_SIZE; va += 32) {
          dccmvac(va);
        }
        for (va = (unsigned long)phy;
             va < (unsigned long)phy + PAGE_SIZE; va += 32) {
          dccmvac(va);
        }
      }
      dmb();
#endif
      page_unmap_on(current->vm->upage, vaddr);
      rt_mutex_lock(&memory_lock);
      mm_free_page(phy);
      rt_mutex_unlock(&memory_lock);
      memory_static(PAGE_SIZE, MEMORY_TYPE_FREE);
    }
    vaddr += PAGE_SIZE;
  }
}

void* kpage_v2p(void* addr, int size) {
  thread_t* current = thread_current();
#ifdef VM_ENABLE
  if (addr == NULL) {
    return NULL;
  }
  if (current != NULL && current->vm != NULL) {
    if (page_v2p(current->vm->upage, addr) != NULL) {
      return addr;
    }
    if (page_v2p(current->vm->kpage, addr) != NULL) {
      return addr;
    }
    if (size > 0) {
      log_error("kpage_v2p unmapped vaddr %x size %x tid %d\n", addr, size,
                current->id);
    }
    return NULL;
  }
#endif
  (void)size;
  return addr;
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
  void* e = NULL;
  int ret = queue_pool_poll(kernel_pool, &e);
  if (ret != 0 || e == NULL) {
    log_error("kpool poll is null\n");
  }
  return e;
}