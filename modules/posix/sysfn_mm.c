/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * 从 sysfn.c 拆出：内存管理（brk/mmap/mremap/munmap/vmap 族）。
 ********************************************************************/
#include "sysfn.h"

#include "kernel/devfn.h"
#include "kernel/device.h"
#include "kernel/error.h"
#include "kernel/event.h"
#include "kernel/fd.h"
#include "kernel/kernel.h"
#include "kernel/page_ref.h" /* memfd 共享页引用计数 */
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
  /* 【诊断·可删】walk 统计：openbox 报 "no free anon region len=1000" 时
   * 需要知道它到底走了多少步、卡在哪个地址、最后一段是被谁挡住的。 */
  int dbg_steps = 0;
  vaddr_t dbg_first = try;
  vaddr_t dbg_last = try;
  while (try >= vm->vaddr) {
    dbg_steps++;
    dbg_last = try;
    if (dbg_steps > 200000) {
      log_error("pick_anon: give up after %d steps len=%lx\n", dbg_steps,
                (unsigned long)length);
      break;
    }
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
        log_error("pick_anon: reach vaddr=%lx steps=%d\n", (unsigned long)try,
                  dbg_steps);
        break;
      }
      try -= PAGE_SIZE;
      continue;
    }
    /* 整段跳过冲突 VMA：落到它的 vaddr 之下（页对齐） */
    if (ov->vaddr < (vaddr_t)length || ov->vaddr - length < vm->vaddr) {
      log_error("pick_anon: blocked by vma %lx-%lx steps=%d len=%lx\n",
                (unsigned long)ov->vaddr, (unsigned long)ov->vend, dbg_steps,
                (unsigned long)length);
      break;
    }
    try = (ov->vaddr - length) & ~(vaddr_t)(PAGE_SIZE - 1);
  }
  log_error("pick_anon fail len=%lx steps=%d first=%lx last=%lx vend=%lx vaddr=%lx\n",
            (unsigned long)length, dbg_steps, (unsigned long)dbg_first,
            (unsigned long)dbg_last, (unsigned long)vm->vend,
            (unsigned long)vm->vaddr);
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
/* 当前 program break（Linux：brk(0) 与失败时的返回值都是它）。 */
static vaddr_t sys_brk_current(vmemory_area_t* vm) {
  if (vm->alloc_addr == 0) {
    vm->alloc_addr = vm->vaddr;
  }
  return vm->alloc_addr;
}

vaddr_t sys_brk(vaddr_t end) {
  thread_t* current = thread_current();
  vmemory_area_t* vm = vmemory_area_find_flag(current->vm->vma, MEMORY_HEAP);
  vaddr_t cur;
  if (vm == NULL) {
    log_error("sys brk not found vm\n");
    return (vaddr_t)-1;
  }
  cur = sys_brk_current(vm);
  if (end == 0) {
    return cur;
  }
  /* 【失败必须返回"当前 break"，不能返回 -1】Linux 的 brk 语义：放不下时
   * 返回当前的 program break（glibc 的 __brk 拿它和请求值比较来判 ENOMEM）。
   * 旧实现返回 -1 ⇒ glibc 的 __curbrk 被写成 0xffffffffffffffff，下一次
   * "brk(__curbrk + size)" 就是一堆垃圾地址（openbox 实测：请求 0x470151000，
   * 高位多出 1<<34），堆彻底失控后在 libc 里 memory fault。 */
  if (end > vm->vend || end < vm->vaddr) {
    log_error("brk: req %lx out of heap [%lx,%lx], keep %lx\n",
              (unsigned long)end, (unsigned long)vm->vaddr,
              (unsigned long)vm->vend, (unsigned long)cur);
    return cur;
  }
  i64 size = (i64)end - (i64)cur;
  if (size > 0) {
    if (sys_mmap_child_overlaps(vm->child, cur, (size_t)size)) {
      log_error("brk: end %lx overlaps mmap, keep %lx\n", (unsigned long)end,
                (unsigned long)cur);
      return cur;
    }
    /* Allocate physical pages for the expanded heap region so that musl
     * mallocng (and any brk-based allocator) can safely write metadata
     * and payloads without hitting unmapped pages. */
    if (valloc((void*)cur, (size_t)size) == NULL) {
      log_error("brk: valloc failed for %lx size %lld\n", (unsigned long)cur,
                (long long)size);
      return cur;
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
#if PROBE_TRACE
  {
    static int mmap_in_cnt = 0;
    if (current != NULL && current->id >= 6 && mmap_in_cnt < 800) {
      log_debug("MMAPIN %d tid=%d addr=%x len=%x prot=%x flags=%x fd=%d off=%x\n",
                mmap_in_cnt++, current->id, (u32)(uintptr_t)addr, (u32)length,
                prot, flags, fd, (u32)pgoffset);
    }
  }
#endif
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

  /* 【帧缓冲 mmap（/dev/fb0 等显示设备）】显存不是页池里的页：驱动 init 时
   * 已用 vmemory_map_phys(..., MEMORY_FB, PAGE_FB) 按【物理地址恒等】映射进
   * 内核页表，并把这条 VMA 注入每个地址空间；用户态首次访问时缺页处理器按
   * PAGE_FB 把物理页镜像到自己的页表（kernel/page.c 的 MEMORY_FB 分支）。
   * 所以这里既不分配页、也不登记新区，直接把已登记的区域"借"给调用方
   * （偏移按字节），返回的地址就是显存地址。
   * 【语义差异（有意）】返回值不在堆/mmap 窗口内 ⇒ munmap 对它是空操作
   * （sys_munmap 里对 MEMORY_FB/MEMORY_DEV 直接放行）；fork 出来的子进程
   * 因为继承 VMA 模板，同样能访问。 */
  if ((flags & MAP_ANON) == 0 && fd >= 0) {
    fd_t* ff = thread_find_fd_id(current, fd);
    vnode_t* fnode = (ff != NULL) ? (vnode_t*)ff->data : NULL;
    device_t* fdev = (fnode != NULL) ? (device_t*)fnode->device : NULL;
    if (fdev != NULL && fdev->type == DEVICE_TYPE_VGA) {
      vaddr_t fb_size = 0;
      vaddr_t fb_base = vmemory_phys_area(MEMORY_FB, &fb_size);
      u64 fb_off;
#if defined(ARM64) || defined(__aarch64__)
      fb_off = (u64)pgoffset;
#else
      fb_off = (u64)pgoffset * PAGE_SIZE;
#endif
      if (fb_base == 0 || fb_size == 0) {
        log_error("mmap fb: no MEMORY_FB area registered\n");
        return MAP_FAILED;
      }
      /* 【按页向上取整再比较】fbdev 的 mmap 长度取 fix.smem_len 向上的页对齐值：
       * Xorg 实测 1920x1080x16bpp 的 smem_len=0x3f4800（不是页倍数），它按页
       * 申请 0x3f5000 ⇒ 超出 0x800 字节。内核映射本来就是按页铺的（最后一页
       * 只用了前 0x800 字节），这种"不足一页的超出"必须放行，否则
       * AddScreen/ScreenInit 直接失败（no screens）。 */
      u64 fb_limit = ((u64)fb_size + PAGE_SIZE - 1) & ~(u64)(PAGE_SIZE - 1);
      if (fb_off + (u64)length > fb_limit) {
        log_error("mmap fb: off=%x len=%x beyond fb size %x (limit %x)\n",
                  (u32)fb_off, (u32)length, (u32)fb_size, (u32)fb_limit);
        return MAP_FAILED;
      }
      return (void*)(uintptr_t)(fb_base + (vaddr_t)fb_off);
    }
  }

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
    /* 【共享页节点（memfd 等）】直接把节点的物理页装配进本进程页表：
     * 多进程看到同一份页，无需 valloc/预读/回写。超出对象长度的尾部
     * 不映射，首次访问由 page.c 的缺页路径补零。 */
    if (node->op != NULL && node->op->fault != NULL) {
      u32 mapped = 0;
      /* 走共享页对象（memfd）分支：多进程映射同一批物理页。 */
      log_debug("mmap shared-mem node=%s addr=%x len=%x off=%x\n",
                node->name != NULL ? node->name : "?", (u32)(uintptr_t)map_at,
                (u32)length, (u32)off);
      if (sys_mmap_install_area(vm, map_at, length, node, off) < 0) {
        log_error("mmap shared-mem install failed addr=%x\n", map_at);
        return MAP_FAILED;
      }
      for (mapped = 0; mapped + PAGE_SIZE <= (u32)length; mapped += PAGE_SIZE) {
        void* pa = node->op->fault(node, off + (u64)mapped);
        if (pa == NULL) {
          continue; /* 超长部分：缺页时补零 */
        }
        page_ref_inc((u32)(uintptr_t)pa);
        page_map_on((page_dir_t*)current->vm->upage,
                    (u32)(uintptr_t)((char*)map_at + mapped),
                    (u32)(uintptr_t)pa, PAGE_USER);
      }
      log_debug("mmap shared-mem addr=%x len=%x off=%x\n", map_at, (u32)length,
                (u32)off);
      return map_at;
    }
    if ((flags & MAP_SHARED) == MAP_SHARED &&
        (off & ((u64)PAGE_SIZE - 1)) == 0 && length >= 16 * 1024 * 1024) {
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
      log_debug("MMAPCHK %s off=%x len=%x filled=%x hdr=%02x%02x%02x%02x\n",
                f->name != NULL ? f->name : "?", (u32)off, (u32)length,
                filled, p[0], p[1], p[2], p[3]);
    }
    /* 【诊断·可删】大文件映射：确认一次性读入是否真的读满（读不满时后面
     * 是零页，动态链接器会报 undefined symbol）。 */
    if (length >= 8 * 1024 * 1024) {
      log_debug("MMAPBIG %s off=%x len=%x filled=%x\n",
                f->name != NULL ? f->name : "?", (u32)off, (u32)length, filled);
    }
    /* 【登记 file_node】仅 MAP_SHARED 登记：MAP_PRIVATE（ld.so 的 libc）
     * 绝不能登记——clone 会把身份带给子进程，共享重映射会把父子 libc
     * 数据页并成同一物理页、退出回写还会把脏数据写进 libc 文件。
     * MAP_SHARED（LTP IPC：子写父读）才需要共享物理页+落盘。 */
    if ((flags & MAP_SHARED) == MAP_SHARED) {
      /* 【诊断·可删】文件型 MAP_SHARED：登记 file_node，跨进程不共享物理页
       * （只保证父子 fork 共享 + 退出回写）。走这条时 wl_shm 会丢内容。 */
      log_debug("MMAPFILE node=%s addr=%x len=%x off=%x\n",
                node->name != NULL ? node->name : "?", (u32)map_at, (u32)length,
                (u32)off);
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
    if (prot != 0) {
      if (valloc(addr, length) == NULL) {
        log_error("map fix valloc failed addr=%x len=%x\n", addr, length);
        return MAP_FAILED;
      }
      /* 匿名 MAP_FIXED 与普通匿名映射一致，必须是零页（见下方 ANON 分支注释） */
      kmemset(addr, 0, length);
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
    /* 【匿名映射必须清零】Linux 保证 MAP_ANONYMOUS 返回全零页。这里 valloc
     * 拿到的是页池里回收来的物理页，里面是上一个进程留下的数据（实测被
     * 0x33 填充过的页），Xorg 直接把 malloc 拿到的"结构体"当链表遍历 ⇒
     * 读到 0x3333333333333333 当指针、在 PixmapSyncDirtyHelper 里 data abort。
     * 注释一直写着"valloc+清零"，但清零这步此前漏了。 */
    kmemset(start_addr, 0, length);
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

/* 【共享页对象（memfd 等）的区间装配】把 vnode 对象里的物理页按 VA 装进
 * 当前进程页表：va 对应的文件偏移 = area->file_off + (va - area->vaddr)。
 * op->fault 返回 NULL（超出对象长度/未分配）的页保持未映射，由缺页路径补零。
 * 与 sys_mmap 的 memfd 分支、page.c 的缺页分支是同一套语义，三处必须一致。 */
static void sys_mmap_attach_shared_range(thread_t* current,
                                         vmemory_area_t* area, vaddr_t va_begin,
                                         vaddr_t va_end) {
  vnode_t* node;
  vaddr_t va;
  if (area == NULL || area->file_node == NULL) {
    return;
  }
  node = (vnode_t*)area->file_node;
  if (node->op == NULL || node->op->fault == NULL) {
    return;
  }
  for (va = va_begin; va < va_end; va += PAGE_SIZE) {
    void* pa;
    if (page_v2p((u64*)current->vm->upage, (void*)va) != NULL) {
      continue; /* 已装配（改大小往返的页）不重复 +ref */
    }
    pa = node->op->fault(node, area->file_off + (u64)(va - area->vaddr));
    if (pa == NULL) {
      continue;
    }
    page_ref_inc((u32)(uintptr_t)pa);
    page_map_on((page_dir_t*)current->vm->upage, (u32)(uintptr_t)va,
                (u32)(uintptr_t)pa, PAGE_USER);
  }
}

/* 该 area 是否是"共享页对象"（memfd 类）：mremap 必须保住共享身份，
 * 不能像匿名/普通文件映射那样 valloc 一批私有零页。 */
static int sys_mmap_area_is_shared(vmemory_area_t* area) {
  vnode_t* node;
  if (area == NULL || area->file_node == NULL) {
    return 0;
  }
  node = (vnode_t*)area->file_node;
  return node->op != NULL && node->op->fault != NULL;
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
  int shared = sys_mmap_area_is_shared(old_area);
  int can_inplace =
      (extend_at + grow <= (u32)vm->vend) &&
      !sys_mmap_child_overlaps(vm->child, extend_at, grow) &&
      !sys_mmap_pages_mapped(current, (void*)(uintptr_t)extend_at, grow);

  if (can_inplace) {
    if (shared) {
      /* 【共享页对象绝不能用 valloc】valloc 会在新增 range 装配一批私有
       * 零页并把 PTE 抢过来 ⇒ 该 range 与对象脱钩（合成器扩容 wl_shm pool
       * 后读客户端写的像素全是 0 的根因）。这里按对象页装配。 */
      old_area->size = new_size;
      old_area->vend = old_area->vaddr + new_size;
      sys_mmap_attach_shared_range(current, old_area, extend_at,
                                   old_area->vend);
      log_debug("mremap inplace shared %x size %x\n", old_area->vaddr,
                new_size);
      return old_address;
    }
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
  if (shared) {
    /* 【共享页对象搬移】不 valloc、不 kmemcpy：新 VMA 直接指向对象的同一批
     * 物理页（内容本来就在对象里，复制既没用又会把私有权带进来）。 */
    if (sys_mmap_install_area(vm, new_addr, new_size, old_area->file_node,
                              old_area->file_off) < 0) {
      log_error("mremap: install shared area failed\n");
      return MAP_FAILED;
    }
    {
      vmemory_area_t* na =
          vmemory_area_find(vm->child, new_addr, new_size);
      if (na != NULL) {
        sys_mmap_attach_shared_range(current, na, na->vaddr, na->vend);
      }
    }
    if (sys_munmap(old_address, map_old) != 0) {
      log_warn("mremap: munmap old %x failed\n", old_address);
    }
    log_debug("mremap moved shared %x -> %x size %x\n", old_address, new_addr,
              new_size);
    return new_addr;
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
  /* 【设备/帧缓冲区直接放行】mmap("/dev/fb0") 只是把驱动登记好的显存区
   * 借出去，没有分配页、也没有 heap child 节点；真去摘 VMA 会让后续访问
   * 找不到物理页。这里按"成功"返回即可。 */
  {
    /* 按"起点是否落在该 area 内"判断（size 传 1），不能用整个 [addr,size)
     * 去匹配：fbdev 的 munmap 长度是 fix.smem_len 向上页对齐后的值，比显存
     * VMA（未对齐的 0x3f4800）大不足一页，严格包含检查会直接不命中，
     * 于是 Xorg 退出清理时报 "munmap: no area intersects" 并留下悬空状态。 */
    vmemory_area_t* pa = vmemory_area_find(current->vm->vma, addr, 1);
    if (pa != NULL &&
        (pa->flags == MEMORY_FB || pa->flags == MEMORY_DEV)) {
      return 0;
    }
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
            current->vm->upage != NULL &&
            !sys_mmap_area_is_shared(p)) { /* 共享页对象无需回写（数据即对象页） */
          vnode_t* fnode = (vnode_t*)p->file_node;
          vaddr_t fva;
          for (fva = s0; fva < s1; fva += PAGE_SIZE) {
            vaddr_t fpa =
                (vaddr_t)page_v2p(current->vm->upage, (void*)(uintptr_t)fva);
            if (fpa == 0) {
              continue;
            }
            (void)vwrite(fnode, (u32)(p->file_off + (u64)(fva - p->vaddr)),
                         PAGE_SIZE, (void*)(uintptr_t)fva);
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
        if (p->vaddr < a0 && a1 < p->vend) {
          /* 【中段挖空必须拆成两块】旧代码只做 p->vend = s0，把 [a1, 旧vend)
           * 整段从 VMA 表里丢掉：那段 VA 页表里还映射着上一份物理页，却不再
           * 被任何 area 覆盖 ⇒ 后续 mmap 复用该 VA 时（尤其文件映射的
           * valloc+kmemset+读入）会写进别人还在用的物理页，造成跨映射串数据
           * （openbox 实测：glibc 堆里出现 0x4_7013_0420 这种 bit34 指针）。
           * 拆成"前段 + 后段"，后段的 file_off 按偏移同步。 */
          vmemory_area_t* tail =
              vmemory_area_create(a1, p->vend - a1, p->flags);
          if (tail != NULL) {
            tail->file_node = p->file_node;
            tail->file_off = p->file_off + (u64)(vaddr_t)(a1 - p->vaddr);
            tail->next = next;
            p->next = tail;
            p->vend = s0;
            next = tail; /* 让循环接着看后段（它与本次 munmap 不相交） */
          } else {
            p->vend = s0;
          }
        } else if (p->vaddr >= a0) {
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