/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * 从 sysfn.c 拆出：内存管理（brk/mmap/mremap/munmap/vmap 族）。
 ********************************************************************/
#include "sysfn.h"

#include "kernel/devfn.h"
#include "kernel/error.h"
#include "kernel/event.h"
#include "kernel/fd.h"
#include "kernel/kernel.h"
#include "kernel/thread.h"
#include "kernel/version.h"
#include "kernel/vfs.h"

#define log_debug

/* ===================== mmap 内部辅助 ===================== */

static int sys_mmap_pages_mapped(thread_t* current, void* addr, size_t length) {
  if (current == NULL || current->vm == NULL || addr == NULL || length == 0) {
    return 0;
  }
  /* 【地址按字长】u32 会在堆区越过 2GB 后回绕/截断 */
  vaddr_t start = (vaddr_t)addr & ~(vaddr_t)(PAGE_SIZE - 1);
  vaddr_t end = ((vaddr_t)addr + length + PAGE_SIZE - 1) & ~(vaddr_t)(PAGE_SIZE - 1);
  for (vaddr_t va = start; va < end; va += PAGE_SIZE) {
    if (page_v2p(current->vm->upage, (void*)va) != NULL) {
      return 1;
    }
  }
  return 0;
}

static vmemory_area_t* sys_mmap_child_overlaps(vmemory_area_t* child, vaddr_t addr, size_t length) {
  vaddr_t end = addr + length;
  for (vmemory_area_t* p = child; p != NULL; p = p->next) {
    if (p->flags == MEMORY_FREE) {
      continue;
    }
    if (addr < p->vend && end > p->vaddr) {
      return p;
    }
  }
  return NULL;
}

static void* sys_mmap_pick_anon_addr(thread_t* current, vmemory_area_t* vm,
                                     size_t length) {
  if (current == NULL || vm == NULL || length == 0) {
    return NULL;
  }
  if ((uintptr_t)vm->vaddr + length > vm->vend) {
    return NULL;
  }
  vaddr_t try = (vm->vend - length) & ~(vaddr_t)(PAGE_SIZE - 1);
  while (try >= vm->vaddr) {
    /* 【VMA 重叠前置 + 整段跳跃】顺序反了是大坑：lazy 文件映射的 VMA
     * 没有已映射页，pages_mapped 恒 0，于是每个 4KB 步都要做一遍
     * O(length/PAGE) 的页表扫描——45MB 匿名映射在 810MB 模型 VMA 底下
     * 逐步下探 = 198k 步 × 11.5k 次 page_v2p ≈ 23 亿次查表（QEMU 实测
     * 卡死数分钟）。先查 VMA（O(n) 链表），重叠就整段跳到冲突 VMA 之下；
     * 只有真无 VMA 重叠才值得做页表扫描。 */
    vmemory_area_t* ov = sys_mmap_child_overlaps(vm->child, try, length);
    if (ov == NULL) {
      if (!sys_mmap_pages_mapped(current, (void*)try, length)) {
        return (void*)try;
      }
      if (try <= vm->vaddr) {
        break;
      }
      try -= PAGE_SIZE;
      continue;
    }
    /* 整段跳过冲突 VMA：落到它的 vaddr 之下（页对齐） */
    if (ov->vaddr < (vaddr_t)length || ov->vaddr - length < vm->vaddr) {
      break;
    }
    try = (ov->vaddr - length) & ~(vaddr_t)(PAGE_SIZE - 1);
  }
  return NULL;
}

static int sys_mmap_install_area(vmemory_area_t* vm, void* start_addr,
                                 size_t length, void* file_node,
                                 u64 file_off) {
  vmemory_area_t* reuse = NULL;
  if (vm->child != NULL) {
    for (vmemory_area_t* p = vm->child; p != NULL; p = p->next) {
      if (p->flags == MEMORY_FREE && p->size >= length &&
          p->vaddr == (vaddr_t)start_addr) {
        reuse = p;
        break;
      }
    }
  }
  if (reuse != NULL) {
    reuse->flags = MEMORY_MMAP;
    reuse->size = length;
    reuse->vend = reuse->vaddr + length;
    reuse->file_node = file_node;
    reuse->file_off = file_off;
    return 0;
  }
  if (vm->child == NULL) {
    vm->child = vmemory_area_create(start_addr, length, MEMORY_MMAP);
    if (vm->child == NULL) return -1;
    vm->child->file_node = file_node;
    vm->child->file_off = file_off;
    return 0;
  }
  vmemory_area_t* last_area = vmemory_area_find_last(vm->child);
  if (last_area == NULL) {
    return -1;
  }
  vmemory_area_t* new_area = vmemory_area_create(start_addr, length, MEMORY_MMAP);
  if (new_area == NULL) {
    return -1;
  }
  new_area->file_node = file_node;
  new_area->file_off = file_off;
  last_area->next = new_area;
  return 0;
}

/* ===================== vmap 族（内核调试/映射） ===================== */

void* sys_vmap(void* addr, size_t size) {
  thread_t* current = thread_current();
  vmemory_area_t* area = vmemory_area_create(
      current->vm->vma->vaddr + current->vm->vma->size, size, MEMORY_HEAP);
  vmemory_area_add(current->vm->vma, area);
  return area->vaddr;
}

void sys_vumap(void* ptr, size_t size) {
  thread_t* current = thread_current();
  vmemory_area_t* area = vmemory_area_find(current->vm->vma, ptr, size);
  if (area == NULL) return;
  vmemory_area_free(area);
}

void* sys_valloc(void* addr, size_t size) { return valloc(addr, size); }

void* sys_vheap() {
  thread_t* current = thread_current();
  return current->vm->vma->alloc_addr;
}

void sys_vfree(void* addr) {
  // todo
  vfree(addr, PAGE_SIZE);
}

/* ===================== brk ===================== */

/* 【地址按字长】参数/返回用 vaddr_t（ARM64=u64 / 32位=u32，见 types.h）：
 * 堆区越过 2GB 后 int/u32 承接会变负；但不能写死 u64——ARM32 的 sys_call_fn
 * 是 u32(*)(u32×6)，按「字」计参（见 sysfn.h 的 ABI 注释）。
 * 行为与旧实现一致（错误返回 -1；end==0 返回当前堆顶；越界/与 mmap 重叠拒绝）。 */
vaddr_t sys_brk(vaddr_t end) {
  thread_t* current = thread_current();
  log_debug("brk tid:%d req:%lx\n", current->id, (unsigned long)end);
  vmemory_area_t* vm = vmemory_area_find_flag(current->vm->vma, MEMORY_HEAP);
  if (vm == NULL) {
    log_error("sys brk not found vm\n");
    return (vaddr_t)-1;
  }
  if (end == 0) {
    if (vm->alloc_addr == 0) {
      vm->alloc_addr = vm->vaddr;
    }
    end = vm->alloc_addr;
    log_debug("brk first ret:%lx\n", (unsigned long)end);
    return end;
  }
  if (end > vm->vend) {
    log_error("brk: end %lx exceeds heap vend %lx, returning -1\n",
              (unsigned long)end, (unsigned long)vm->vend);
    return (vaddr_t)-1;
  }
  i64 size = (i64)end - (i64)vm->alloc_addr;
  if (size > 0) {
    if (sys_mmap_child_overlaps(vm->child, vm->alloc_addr, (size_t)size)) {
      log_error("brk: end %lx overlaps mmap, keep %lx\n",
                (unsigned long)end, (unsigned long)vm->alloc_addr);
      return (vaddr_t)-1;
    }
    /* Allocate physical pages for the expanded heap region so that musl
     * mallocng (and any brk-based allocator) can safely write metadata
     * and payloads without hitting unmapped pages. */
    if (valloc((void*)vm->alloc_addr, (size_t)size) == NULL) {
      log_error("brk: valloc failed for %lx size %lld\n",
                (unsigned long)vm->alloc_addr, (long long)size);
      return (vaddr_t)-1;
    }
  }
  if (size < 0) {
    log_debug("brk shrink %lx by %lld\n", (unsigned long)end,
              (long long)-size);
    vfree((void*)end, (size_t)(-size));
  }
  vm->alloc_size += size;
  vm->alloc_addr = end;
  log_debug("brk old:%lx new:%lx size_delta:%lld total:%lld\n",
            (unsigned long)(end - size), (unsigned long)end,
            (long long)size, (long long)vm->alloc_size);
  return end;
}

/* ===================== mmap / mremap / munmap ===================== */

void* sys_mmap2(void* addr, size_t length, int prot, int flags, int fd,
                size_t pgoffset) {
  int ret = 0;
  thread_t* current = thread_current();
  {
    static int mmap_in_cnt = 0;
    if (current != NULL && current->id >= 6 && mmap_in_cnt < 800) {
      log_error("MMAPIN %d tid=%d addr=%x len=%x prot=%x flags=%x fd=%d off=%x\n",
                mmap_in_cnt++, current->id, (u32)(uintptr_t)addr, (u32)length,
                prot, flags, fd, (u32)pgoffset);
    }
  }
  vmemory_area_t* vm = vmemory_area_find_flag(current->vm->vma, MEMORY_HEAP);
  if (vm == NULL) {
    log_error("sys mmap2 not found vm\n");
    return MAP_FAILED;
  }
#ifdef LOG_MMAP
  log_debug(
      "sys mmap2 addr=%x length=%d prot = %x,flags = %x, fd = %d, "
      "pgoffset = %d "
      "\n",
      addr, length, prot, flags, fd, pgoffset);
#endif
  if (length <= 0) {
    log_error("map failed length 0\n");
    return MAP_FAILED;
  }

  // 内存大小 对齐 16 page-aligned
  length = ALIGN(length, PAGE_SIZE);

  /* 匿名映射忽略 fd。其余 fd>=0 是文件映射，ld.so 靠它装 libc。
   * aarch64 的偏移是字节；arm 的 mmap2 偏移是页。 */
  if ((flags & MAP_ANON) == 0 && fd >= 0) {
    fd_t* f = thread_find_fd_id(current, fd);
    vnode_t* node;
    void* map_at;
    u64 off;
    u32 done;
    u32 filled = 0;
    if (f == NULL || f->data == NULL) {
      log_error("mmap file not found fd %d tid %d\n", fd, current->id);
      return MAP_FAILED;
    }
    node = (vnode_t*)f->data;
#if defined(ARM64) || defined(__aarch64__)
    off = (u64)pgoffset;
#else
    off = (u64)pgoffset * PAGE_SIZE;
#endif
    if ((flags & MAP_FIXED) == MAP_FIXED) {
      if (addr == NULL ||
          vmemory_area_find(current->vm->vma, addr, length) == NULL) {
        log_error("mmap file fixed %x out of range\n", addr);
        return MAP_FAILED;
      }
      map_at = addr;
    } else {
      map_at = sys_mmap_pick_anon_addr(current, vm, length);
      if (map_at == NULL) {
        log_error("mmap file: no free region len=%x\n", length);
        return MAP_FAILED;
      }
    }
    /* 【按需分页】页对齐偏移且【映射足够大】才走登记式文件映射：不 valloc、
     * 不读文件，物理页在缺页时由 page.c 逐页 valloc+vread 补内容。
     * 阈值的由来：musl ld.so 是 mmap 完就 close(fd)（dynlink.c map_library
     * 之后紧跟 close），fatfs close 会关 FIL ⇒ 懒页缺页时 vread 全 0 ⇒
     * shell 一起就崩（实测 2026-10）。小映射（libc 几 MB）继续走旧的
     * 一次性读入路径（fd 关闭前内容已在内存）；大文件（yllm 810MB 模型，
     * wmap_open 保持 fd 打开）才走 lazy。 */
    if ((off & ((u64)PAGE_SIZE - 1)) == 0 &&
        length >= 16 * 1024 * 1024) {
      if (sys_mmap_install_area(vm, map_at, length, node, off) < 0) {
        log_error("mmap file install failed addr=%x\n", map_at);
        return MAP_FAILED;
      }
      log_debug("mmap file lazy addr %x len %x off %x\n", map_at,
                (u32)length, (u32)off);
      return map_at;
    }
    if (valloc(map_at, length) == NULL) {
      log_error("mmap file valloc failed addr=%x len=%x\n", map_at, length);
      return MAP_FAILED;
    }
    kmemset(map_at, 0, length);
    while (filled < (u32)length) {
      u32 chunk = (u32)length - filled;
      if (chunk > 4096) {
        chunk = 4096;
      }
      if (off + filled > 0xffffffffull) {
        break;
      }
      done = vread(node, (u32)(off + filled), chunk, (u8*)map_at + filled);
      if (done == 0) {
        break;
      }
      filled += done;
      if (done < chunk) {
        break;
      }
    }
    if (filled == 0 && node->length > (u32)off) {
      log_error("mmap file read 0 %s off=%x len=%x\n",
                f->name != NULL ? f->name : "", (u32)off, (u32)length);
      return MAP_FAILED;
    }
    {
      u8* p = (u8*)map_at;
      log_error("MMAPCHK %s off=%x len=%x filled=%x hdr=%02x%02x%02x%02x\n",
                f->name != NULL ? f->name : "?", (u32)off, (u32)length,
                filled, p[0], p[1], p[2], p[3]);
    }
    /* 【登记 file_node】仅 MAP_SHARED 登记：MAP_PRIVATE（ld.so 的 libc）
     * 绝不能登记——clone 会把身份带给子进程，共享重映射会把父子 libc
     * 数据页并成同一物理页、退出回写还会把脏数据写进 libc 文件。
     * MAP_SHARED（LTP IPC：子写父读）才需要共享物理页+落盘。 */
    if ((flags & MAP_SHARED) == MAP_SHARED) {
      if (sys_mmap_install_area(vm, map_at, length, node, off) < 0) {
        log_error("mmap file install failed addr=%x\n", map_at);
        return MAP_FAILED;
      }
    }
    return map_at;
  }

  if ((flags & MAP_FIXED) == MAP_FIXED) {
    vmemory_area_t* findvm = vmemory_area_find(current->vm->vma, addr, length);
    if (findvm == NULL) {
      log_error("map fix %x faild out of range\n", addr);
      return MAP_FAILED;
    }
    if (prot != 0 && valloc(addr, length) == NULL) {
      log_error("map fix valloc failed addr=%x len=%x\n", addr, length);
      return MAP_FAILED;
    }
    if (sys_mmap_install_area(vm, addr, length, NULL, 0) < 0) {
      log_error("map fix install area failed addr=%x\n", addr);
      return MAP_FAILED;
    }
    /* 【匿名 MAP_SHARED 标记】类型改为 MEMORY_SHARE，fork 时 vmemory_clone
     * 据此把子进程重映射到父进程物理页（真共享）；MAP_PRIVATE 不标。 */
    if ((flags & MAP_SHARED) == MAP_SHARED) {
      vmemory_area_t* area = vmemory_area_find(vm->child, addr, length);
      if (area != NULL) {
        area->flags = MEMORY_SHARE;
      }
    }
    return addr;
  }

  void* start_addr = NULL;
    if ((flags & MAP_ANON) == MAP_ANON) {
    start_addr = sys_mmap_pick_anon_addr(current, vm, length);
    if (start_addr == NULL) {
      log_error("mmap: no free anon region len=%x\n", length);
      return MAP_FAILED;
    }
    /* 【anon 无条件分配物理页】mallocng 的 meta area 走 mmap(PROT_NONE)+
     * mprotect(RW) 两步；内核 mprotect 未实现(ENOSYS)，mallocng 容忍后直接
     * 写这页。若 prot=0 时缺页靠 page_fault 补，时序上时好时坏 ⇒ 堆元数据
     * 偶发损坏（gnuboy/testmalloc 实测 a_crash）。这里无条件 valloc+清零，
     * 让两步流程闭环；PROT_NONE 的"不可访问"语义在单任务场景可接受。 */
    if (valloc(start_addr, length) == NULL) {
      log_error("mmap anon valloc failed addr=%x len=%x\n", start_addr, length);
      return MAP_FAILED;
    }
    if (sys_mmap_install_area(vm, start_addr, length, NULL, 0) < 0) {
      log_error("mmap install area failed addr=%x\n", start_addr);
      return MAP_FAILED;
    }
    /* 【匿名 MAP_SHARED 标记】LTP futex 系/clone03 用
     * mmap(MAP_SHARED|MAP_ANONYMOUS) 做父子共享区。fork 的深拷贝+COW 会让
     * 子进程一写就私有化 ⇒ 父进程读不到。类型改 MEMORY_SHARE 后
     * vmemory_clone 会把子进程 PTE 重映射到父进程当前物理页（真共享）。 */
    if ((flags & MAP_SHARED) == MAP_SHARED) {
      vmemory_area_t* area = vmemory_area_find(vm->child, start_addr, length);
      if (area != NULL) {
        area->flags = MEMORY_SHARE;
      }
    }
    log_debug("mmap anon addr %x len %x prot %x\n", start_addr, length, prot);
    return start_addr;
  }

  // Legacy/file-backed path below (still unused for musl TLS).
  start_addr = addr;

  // 先找合适大小的已释放节点复用，避免地址单向增长导致堆溢出
  vmemory_area_t* reuse = NULL;
  if (vm->child != NULL) {
    for (vmemory_area_t* p = vm->child; p != NULL; p = p->next) {
      if (p->flags == MEMORY_FREE && p->size >= length &&
          !sys_mmap_pages_mapped(current, (void*)p->vaddr, length)) {
        reuse = p;
        break;
      }
    }
  }

  if (reuse != NULL) {
    reuse->flags = MEMORY_MMAP;
    reuse->size = length;
    reuse->vend = reuse->vaddr + length;
    reuse->file_node = NULL; /* 复用 MEMORY_FREE 节点：清掉上次映射的残留 */
    reuse->file_off = 0;
    start_addr = (void*)reuse->vaddr;
  } else {
    if (vm->child == NULL) {  // 未分配过
      start_addr = vm->vaddr + vm->size / 2;
    } else {
      vmemory_area_t* last_area = vmemory_area_find_last(vm->child);
      start_addr = last_area->vend;
    }

    if ((uintptr_t)start_addr + length > vm->vend) {
      log_error("mmap: out of heap range start=%x len=%x vend=%x\n",
                start_addr, length, vm->vend);
      return MAP_FAILED;
    }

    if (vm->child == NULL) {
      vm->child = vmemory_area_create(start_addr, length, MEMORY_MMAP);
    } else {
      vmemory_area_t* last_area = vmemory_area_find_last(vm->child);
      vmemory_area_t* new_area = vmemory_area_create(start_addr, length, MEMORY_MMAP);
      last_area->next = new_area;
    }
  }

  if ((flags & MAP_SHARED) == MAP_SHARED) {
#ifdef LOG_MMAP
    log_debug("map shared return addr %x\n", start_addr);
#endif
    if (prot != 0 && valloc(start_addr, length) == NULL) {
      log_error("mmap shared valloc failed addr=%x len=%x\n", start_addr, length);
      return MAP_FAILED;
    }
    return start_addr;
  } else if ((flags & MAP_PRIVATE) == MAP_PRIVATE) {
    log_debug("map private return addr %x\n", start_addr);
    if (prot != 0 && valloc(start_addr, length) == NULL) {
      log_error("mmap private valloc failed addr=%x len=%x\n", start_addr, length);
      return MAP_FAILED;
    }
    return start_addr;
  }
  log_error("map failed end\n");
  return MAP_FAILED;
}

void* sys_mremap(void* old_address, size_t old_size, size_t new_size, int flags,
                 ... /* void *new_address */) {
  (void)old_size;
  thread_t* current = thread_current();
  if (current == NULL || current->vm == NULL) {
    return MAP_FAILED;
  }
  vmemory_area_t* vm = vmemory_area_find_flag(current->vm->vma, MEMORY_HEAP);
  if (vm == NULL) {
    log_error("sys mremap not found vm\n");
    return MAP_FAILED;
  }
  if (old_address == NULL || new_size == 0) {
    return MAP_FAILED;
  }
  if ((flags & MREMAP_FIXED) == MREMAP_FIXED) {
    log_error("mremap: MREMAP_FIXED not supported\n");
    return MAP_FAILED;
  }

  new_size = ALIGN(new_size, PAGE_SIZE);

  log_debug("sys mremap old=%x new=%x flags=%x\n", old_address, new_size, flags);

  vmemory_area_t* old_area = vmemory_area_find(vm->child, old_address, 0);
  if (old_area == NULL || old_area->flags == MEMORY_FREE) {
    log_error("mremap: area not found at %x\n", old_address);
    return MAP_FAILED;
  }
  /* mallocng passes the mapping base (g->mem). */
  if ((vaddr_t)old_address != old_area->vaddr) {
    log_error("mremap: %x is not mapping base %x\n", old_address,
              old_area->vaddr);
    return MAP_FAILED;
  }

  size_t map_old = old_area->size;
  if (new_size == map_old) {
    return old_address;
  }

  /* Shrink: drop trailing pages. */
  if (new_size < map_old) {
    vfree((void*)(old_area->vaddr + new_size), map_old - new_size);
    old_area->size = new_size;
    old_area->vend = old_area->vaddr + new_size;
    return old_address;
  }

  size_t grow = new_size - map_old;
  u32 extend_at = (u32)old_area->vend;
  int can_inplace =
      (extend_at + grow <= (u32)vm->vend) &&
      !sys_mmap_child_overlaps(vm->child, extend_at, grow) &&
      !sys_mmap_pages_mapped(current, (void*)(uintptr_t)extend_at, grow);

  if (can_inplace) {
    if (valloc((void*)(uintptr_t)extend_at, grow) == NULL) {
      log_error("mremap: inplace valloc failed at %x len %x\n", extend_at,
                grow);
      return MAP_FAILED;
    }
    old_area->size = new_size;
    old_area->vend = old_area->vaddr + new_size;
    log_debug("mremap inplace %x size %x\n", old_area->vaddr, new_size);
    return old_address;
  }

  if ((flags & MREMAP_MAYMOVE) == 0) {
    log_error("mremap: cannot grow in place\n");
    return MAP_FAILED;
  }

  /*
   * Old code only bumped vend / created a VMA and never mapped pages or
   * copied — realloc(256K→512K) then wrote into unmapped VA (PREF/data abort).
   */
  void* new_addr = sys_mmap_pick_anon_addr(current, vm, new_size);
  if (new_addr == NULL) {
    log_error("mremap: no anon space for %x\n", new_size);
    return MAP_FAILED;
  }
  if (valloc(new_addr, new_size) == NULL) {
    log_error("mremap: valloc new %x len %x failed\n", new_addr, new_size);
    return MAP_FAILED;
  }
  if (sys_mmap_install_area(vm, new_addr, new_size, NULL, 0) < 0) {
    vfree(new_addr, new_size);
    log_error("mremap: install area failed\n");
    return MAP_FAILED;
  }

  kmemcpy(new_addr, old_address, map_old);
  if (sys_munmap(old_address, map_old) != 0) {
    log_warn("mremap: munmap old %x failed\n", old_address);
  }

  log_debug("mremap moved %x -> %x size %x\n", old_address, new_addr, new_size);
  return new_addr;
}

int sys_munmap(void* addr, size_t size) {
  log_debug("sys munmap addr: %lx size: %llu\n", (unsigned long)addr,
            (unsigned long long)size);
  thread_t* current = thread_current();
  vmemory_area_t* vm = vmemory_area_find_flag(current->vm->vma, MEMORY_HEAP);
  if (vm == NULL) {
    log_error("sys munmap not found vm\n");
    return MAP_FAILED;
  }
  /* YiYiYa·修复：支持一次 munmap 覆盖【多个/部分】area ✗→✓
   * 原实现要求 [addr, addr+size) 完全落在【单个】area 内；而 mallocng 的 trim
   * 会把多个相邻 mmap 合并成一次 munmap ⇒ 永远 area==NULL ⇒ 静默 return ⇒
   * 物理页/映射【从不释放】⇒ 页池耗尽 ⇒ valloc failed / ya_sbrk oom 卡死 ✗✗。
   * 现在：与 [addr,end) 相交的 area 逐个 vfree；完全包含的整块摘除，
   * 部分重叠的裁剪端点（另一端保持；中间被挖空时重复 vfree 是无害的 ✓）。 */
  {
    vaddr_t a0 = (vaddr_t)addr;
    vaddr_t a1 = a0 + size;
    int freed = 0;
    vmemory_area_t* prev = NULL;
    vmemory_area_t* p = vm->child;
    while (p != NULL) {
      vmemory_area_t* next = p->next;
      if (a0 < p->vend && p->vaddr < a1) {
        vaddr_t s0 = a0 > p->vaddr ? a0 : p->vaddr;
        vaddr_t s1 = a1 < p->vend ? a1 : p->vend;
        /* 【MAP_SHARED 落盘】物理页即将释放，文件映射的脏页先写回文件，
         * 否则 munmap→close→重开读不到写入内容（LTP IPC 走这条路径）。 */
        if (p->file_node != NULL && current->vm != NULL &&
            current->vm->upage != NULL) {
          vnode_t* fnode = (vnode_t*)p->file_node;
          vaddr_t fva;
          for (fva = s0; fva < s1; fva += PAGE_SIZE) {
            vaddr_t fpa =
                (vaddr_t)page_v2p(current->vm->upage, (void*)(uintptr_t)fva);
            if (fpa == 0) {
              continue;
            }
            {
              u32 wr = vwrite(fnode, (u32)(p->file_off + (u64)(fva - p->vaddr)),
                              PAGE_SIZE, (void*)(uintptr_t)fva);
              static int dbg_mfl;
              if (dbg_mfl++ < 8) {
                kprintf("MFLUSH area %x-%x node=%x off=%d pa=%x wr=%d\n",
                        (u32)s0, (u32)s1, (u32)(uintptr_t)fnode,
                        (u32)(p->file_off + (u64)(fva - p->vaddr)), (u32)fpa,
                        wr);
              }
            }
          }
        }
        vfree((void*)s0, (vaddr_t)(s1 - s0));
        freed++;
        if (a0 <= p->vaddr && a1 >= p->vend) {
          /* 完全包含 ⇒ 摘除整块 ✓ */
          if (prev == NULL) {
            vm->child = next;
          } else {
            prev->next = next;
          }
          kfree(p);
          p = next; /* prev 不变 ✓ */
          continue;
        }
        /* 部分重叠 ⇒ 裁剪端点（保留剩余）✓ */
        if (p->vaddr >= a0) {
          /* 裁头：剩余区起点从旧 vaddr 前移到 s1，文件映射的
           * file_off 必须同步后移，缺页时 off=vaddr差值 才对得上。 */
          p->file_off += (u64)(vaddr_t)(s1 - p->vaddr);
          p->vaddr = s1;
        } else {
          p->vend = s0;
        }
      }
      prev = p;
      p = next;
    }
    if (freed == 0) {
      static u32 dbg_unmap_miss = 0;
      if (dbg_unmap_miss < 20) {
        dbg_unmap_miss++;
        log_error("munmap: no area intersects %lx len=%llu\n",
                  (unsigned long)a0, (unsigned long long)size);
      }
    }
    return 0;
  }
}

int sys_mprotect(const void* start, size_t len, int prot) {
  (void)start;
  (void)len;
  (void)prot;
  /* 页并不按读/写/执行分开。直接成功，避免 ld.so 做只读重定位时退出。 */
  return 0;
}

int sys_madvice(void* addr, size_t length, int advice) {
  log_debug("sys madvice not impl %x len:%x %x\n", addr, length, advice);
  return 0;
}

int sys_msync(void* addr, size_t length, int flags) {
  (void)addr;
  (void)length;
  (void)flags;
  return 0; /* 我们的 mmap 不缓存写回时机，无脏数据可刷 */
}

int sys_mlockall(int flags) {
  (void)flags;
  return 0;
}