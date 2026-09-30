/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "memory.h"
#include "page.h"
#include "page_ref.h"
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

/* 【只释放节点内存】页/页表已由 page_destroy 统一归还（见 vmemory_destroy），
 * 这里绝不能 vmemory_area_free 每个 area（那会 vfree 页 ⇒ 重复归还 ✗）。
 * child 是同地址 mmap 子链（vmemory_area_clone 递归拷贝），一并 kfree。 */
void vmemory_area_destroy_list(vmemory_area_t* area) {
  while (area != NULL) {
    vmemory_area_t* next = area->next;
    if (area->child != NULL) {
      vmemory_area_destroy_list(area->child);
    }
    kfree(area);
    area = next;
  }
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
  area->attr = PAGE_USER; /* 默认用户可缓存页；DEV/FB 等用 create_attr 显式指定 */
  area->child = NULL;
  return area;
}

vmemory_area_t* vmemory_area_create_attr(void* addr, vaddr_t size, u8 flags,
                                         u8 attr) {
  vmemory_area_t* area = vmemory_area_create(addr, size, flags);
  if (attr) {
    area->attr = attr;
  }
  return area;
}

/* 需要"内核页表映射 + 注入每个进程 VMA 模板"的物理区域（见 memory.h）。
 * 内核不反向依赖具体驱动，驱动在 init 里调 vmemory_map_phys() 登记即可。
 * 直接复用 vmemory_area_t 作模板链，不另造结构体。 */
static vmemory_area_t* s_phys_vma = NULL;

void vmemory_map_phys(vaddr_t vaddr, vaddr_t paddr, vaddr_t size, u8 flags,
                      u8 attr) {
  if (size == 0) {
    return;
  }
  if (paddr == 0) {
    paddr = vaddr; /* 恒等映射 */
  }
  /* 物理地址进内核页表；VMA 带上 attr，用户态缺页按需镜像时直接用。
   * 同一 vaddr 重复登记时只更新几何/属性，不重复注入 VMA。 */
  vmemory_area_t* a = NULL;
  for (vmemory_area_t* p = s_phys_vma; p != NULL; p = p->next) {
    if (p->vaddr == vaddr) {
      a = p;
      break;
    }
  }
  if (a == NULL) {
    a = vmemory_area_create_attr((void*)vaddr, size, flags, attr);
    a->alloc_addr = vaddr;
    a->alloc_size = size;
    if (s_phys_vma == NULL) {
      s_phys_vma = a;
    } else {
      vmemory_area_add(s_phys_vma, a);
    }
  } else {
    a->size = size;
    a->vend = vaddr + size;
    a->flags = flags;
    a->attr = attr;
    a->alloc_addr = vaddr;
    a->alloc_size = size;
  }
  for (vaddr_t off = 0; off < size; off += PAGE_SIZE) {
    page_map(vaddr + off, paddr + off, a->attr);
  }
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
    c->attr = p->attr;
    if (flag == 1) {
      c->alloc_addr = p->alloc_addr;
      c->alloc_size = p->alloc_size;
    }
    // Recursively clone mmap child list so forked processes have correct VMA
    if (p->child != NULL) {
      c->child = vmemory_area_clone(p->child, flag);
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

  /* mmap 不再单开专用 VA 区：见 memory.h 里 ANON_ADDR 被移除的说明 ——
   * 之前把 mmap 区放在 0x50000000（EXEC_ADDR=0x60000000 **之下**），而各架构
   * 管理用户页表的扫描范围都是 [EXEC_ADDR, 0x80000000) ⇒ 落在外面的 mmap
   * 映射在进程退出时不会被解映射/回收（物理页与 L2 表泄漏），fork 克隆用户区
   * 也会漏掉它。现在 sys_mmap 走堆区（100MB，与专用区同容量），靠
   * sys_mmap_vma_overlaps() 避开所有已有 VMA 来防撞车。 */

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
  /* 把 vmemory_map_phys() 登记过的设备/显存区域注入本进程的 VMA 链
   * （缺页时用户态才按 flags 镜像;内核页表映射已在那一步完成）。 */
  for (vmemory_area_t* m = s_phys_vma; m != NULL; m = m->next) {
    vmemory_area_t* a =
        vmemory_area_create_attr((void*)m->vaddr, m->size, m->flags, m->attr);
    a->alloc_addr = m->alloc_addr;
    a->alloc_size = m->size;
    vmemory_area_add(vmm, a);
  }

  return vmm;
}

void vmemory_dump(vmemory_t* vm) {
  char* type[] = {"free", "use",   "share", "heap", "exec",
                  "data", "stack", "mmap",  "dev",  "fb"};
  vmemory_area_t* p = vm->vma;
  for (; p != NULL; p = p->next) {
    log_debug(
        "tid %d vaddr:%x vend:%x size:%x alloc p:%x size:%x flag:%x type:%s\n",
        vm->tid, p->vaddr, p->vend, p->size, p->alloc_addr, p->alloc_size,
        p->flags, type[p->flags]);
  }
}

void vmemory_dump_area(vmemory_area_t* area) {
  char* type[] = {"free", "use",   "share", "heap", "exec",
                  "data", "stack", "mmap",  "dev",  "fb"};
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
                PAGE_USER);
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
  vm->ref = 1;
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
    addr = vm->vaddr;
    end_addr = vm->vend;
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
    /* 【泄漏修复】这里原来 kmalloc 一块 4KB 内核堆内存，然后直接把它当"物理
     * 页"映射给子进程（vmemory_map 第三参语义是物理地址，本内核堆恰好恒等
     * 映射所以能跑），并且从不归还 —— fork 每复制一页就漏 4KB（MMPROF 实测：
     * 4KB×1805 次 / 10 次 ls ≈ 7.4MB，几轮就把内核堆吃光）。
     * 改用页分配器：子进程退出时 vfree→mm_free_page 会把它还给页分配器。 */
    void* copy_addr = mm_alloc_page();
    if (copy_addr == NULL) {
      log_error("tid %d vm copy %s page alloc failed at %lx\n", vm_copy->tid,
                type_str, addr);
      continue;
    }
    /* 【引用计数】拷贝页是本进程独占的（不是 COW 共享），登记 ref=1：
     * 否则它和"page_copy 共享给子进程的父页"一样是 ref==0，exec 回收时
     * 无法区分 ⇒ 只能不还页（泄漏）。登记后 exec/退出都能正常归还。 */
    page_ref_set((u32)(uintptr_t)copy_addr, 1);
    void* phy = page_v2p((u64*)vm_src->upage, (void*)addr);
    if (phy != NULL) {
      kmemmove(copy_addr, (void*)addr, PAGE_SIZE);
      log_debug("-copy vaddr %lx addr %lx to %lx\n", addr, phy, copy_addr);
    } else {
      kmemset(copy_addr, 0, PAGE_SIZE);
      log_debug("-copy vaddr %lx zero page %lx\n", addr, copy_addr);
    }
    /* 这页刚被 CPU 写过（恒等映射），先 clean+invalidate 让内存拿到最新内容：
     * 子进程稍后用自己的虚拟地址读同一物理页，两份映射在 cache 里不保证是
     * 同一条行，不同步就会读到旧数据（接口语义见 arch/cpu.h）。 */
    cpu_flush_dcache_range((unsigned long)copy_addr,
                           (unsigned long)copy_addr + PAGE_SIZE);
    vmemory_map(vm_copy->upage, addr, (vaddr_t)copy_addr, PAGE_SIZE);
    copied_pages++;
  }
  // Always inherit the parent's brk/alloc pointers regardless of how many
  // pages were physically copied; uncopied lazy pages will be demand-paged.
  cvm->alloc_addr = vm->alloc_addr;
  cvm->alloc_size = vm->alloc_size;
#endif
}

/* 【回收地址空间】把 vm 独占的用户物理页归还页分配器。
 * 只处理 ref<=1（fork 出来的独占空间）；VM_SAME（pthread 共享）的 vm 被多个
 * 线程共用且引用计数不完整，这里不动它（避免释放仍被使用的页）。
 * 注意：不能用 vfree() —— 它只在"当前地址空间"里有效，而被回收线程的页表
 * 并不是当前的；所以这里直接遍历它自己的页表。只扫用户 VA 区间：内核区
 * （0x82000000+）的页表是共享的，绝不能释放。
 * 【归还页表本身】用户区扫完后，把区间内每张 L2 表（1KB/张）和 L1 表
 * （16KB，含 16KB 对齐的头）一并 mm_free_align 归还内核堆 —— 它们都是本
 * 进程私有的（page_clone 深拷贝 / page_map_on 按需分配），且此刻本进程
 * 已死、页表已不被 MMU 使用（回收发生在切换到其它地址空间之后）。此前
 * 每个死进程要漏 16KB + n×1KB 的内核堆，跑 64 次 exec 就漏 1MB+。
 * LEVEL_KERNEL_SHARE 的 upage 就是全局共享内核页目录（== vm->kpage），
 * 绝不能释放，直接跳过。 */
void vmemory_destroy(vmemory_t* vm) {
  if (vm == NULL) {
    return;
  }
  if (vm->ref > 1) {
    return;
  }
#ifdef VM_ENABLE
  if (vm->upage != NULL) {
    if (vm->kpage != NULL && vm->upage == vm->kpage) {
      /* 共享内核页目录（LEVEL_KERNEL_SHARE）：不属于本线程，绝不释放 */
      return;
    }
    /* 页表遍历归还（用户物理页 + 各级表本身）是架构相关逻辑
     * （表级数/项宽/掩码/共享内核区判定都不同），统一由各 arch 的
     * mm.c 实现 page_destroy()。此处只负责私有线程页表的释放时机。
     * 置 NULL 防止 vm 悬挂后还被误用。 */
    page_destroy(vm->upage);
    vm->upage = NULL;
  }
#endif
  /* 【vma 节点释放】页/页表已由 page_destroy 归还；vma 链表节点是 kmalloc
   * 出来的（vmemory_area_create），此前无人释放 ⇒ 每次 fork/exec 漏 N 个
   * 节点。VM_SAME（pthread）时 vma 指针共享：ref>1 已提前 return；
   * "最后一个成员"由 thread_recycle_process 置 ref=1 后走到这里，一并释放 ✓。
   * 注意上面"共享内核页目录"的 return 分支：那种 vm（内核线程）的 vma 不动。 */
  if (vm->vma != NULL) {
    vmemory_area_destroy_list(vm->vma);
    vm->vma = NULL;
  }
}

void vmemory_clone(vmemory_t* vmcopy, vmemory_t* vmthread, u32 flags) {
  log_debug("vm clone init flags=%x\n", flags);

  if ((flags & VM_SAME) != 0) {
    /* pthread CLONE_VM: share page tables and VMA list with parent. */
    vmcopy->vma = vmthread->vma;
    vmcopy->kpage = vmthread->kpage;
    vmcopy->upage = vmthread->upage;
    if (vmthread->ref == 0) {
      vmthread->ref = 1;
    }
    vmthread->ref++;
    vmcopy->ref = vmthread->ref;
    log_debug("vm share upage %x ref %d\n", vmcopy->upage, vmcopy->ref);
    return;
  }

  vmcopy->vma = vmemory_area_clone(vmthread->vma, 1);
  vmcopy->kpage = page_kernel_dir();
#ifdef CONFIG_COW
  /* COW 策略层：fork 前把父进程可写的 EXEC 段页改成"只读+COW"，
   * 之后 page_clone 复制出的子页表自然是只读的（父子共享物理页）。
   * 必须【先改父页表再复制】，否则子 PTE 会是可写、写时绕过 COW。
   * 这里只决定"哪些 VMA 做 COW"，PTE 机制在 arch 的 page_cow_apply。 */
  {
    /* arch 只提供 page_cow_apply（改 PTE）与 page_cow_query/page_v2p；
     * 引用计数（ref++）由 kernel 在这里做。 */
    vmemory_area_t* a;
    for (a = vmthread->vma; a != NULL; a = a->next) {
      /* 【别只看 EXEC】应用真正"装入的映像/数据"在 HEAP 区（见 vmemory_create_default：
       * mmap 与装载都走堆区 0x70100000+），只标 EXEC 等于 COW 没开 —— 实测 armv8-a
       * 日志里一条 COW-mark 都没有、page fault at 0 照旧 ✗。
       * EXEC + HEAP 都标；未映射的页由下面 page_v2p 过滤掉（100MB 区多是空洞，很便宜）。 */
      if (a->flags != MEMORY_EXEC && a->flags != MEMORY_HEAP) {
        continue;
      }
      u32 s = (u32)a->vaddr;
      u32 e = (u32)a->vend;
      for (u32 va = s; va < e; va += PAGE_SIZE) {
        u32 pa = 0;
        /* 【必须跳过"内核映像段"】内核自己的段也被登记成 MEMORY_EXEC
         * （见本文件 191-200：boot_info->segments ⇒ vmemory_area_create），
         * 它们不是"用户私有代码"：
         *   · armv7-a 标了没事（AP[1:0]=0b10 仍让【特权可写】✓）；
         *   · armv8-a 标了就致命 —— PTE_AP_EL1_RO 让 EL1 也变只读 ✗，
         *     内核一写自己的 .data/.bss 就 fault ⇒ 早期内核崩（实测
         *     tid:0 page fault at 2/10、tid 变垃圾值 2569472、风暴）✗。
         * 用户 EXEC 预留区从 EXEC_ADDR 起，故按 EXEC_ADDR 切一刀。 */
        if (va < (u32)EXEC_ADDR) {
          continue;
        }
        /* 【跳过未映射的页】EXEC 段里含大段"预留区"（如 armv8-a 的
         * 0x60000000-66400000，实测整段 pa=0），标它既无意义又白占时间；
         * 真正要保护的是已装入的代码页。 */
        if (page_v2p((u64*)vmthread->upage, (void*)(uintptr_t)va) == NULL) {
          continue;
        }
        page_cow_apply((u32*)vmthread->upage, va, 0); /* 标 COW（只读+标记） */
        if (page_cow_query((u32*)vmthread->upage, va, &pa)) {
          page_ref_inc(pa); /* 引用计数在 kernel 侧 */
        }
      }
    }
  }
#endif
  vmcopy->upage = page_clone((u64*)vmthread->upage, 3);
  if (vmcopy->upage == NULL) {
    log_error("vm clone: page_clone failed\n");
    return;
  }
  vmcopy->ref = 1;

  /* 栈/堆仍然深拷贝：ARMv5 的 COW 只读页是 "特权可写、用户只读"(AP=10)，
   * 内核通过用户 VA 写（exec 建栈、copy_to_user）不会触发 fault，会直接改
   * 共享物理页 → 破坏父进程。栈/堆是内核也频繁写的区域，必须独立。
   * COW 只用在 EXEC 段的 .data（见 page_copy）。 */
  // 栈拷贝并映射
  vmemory_copy_data(vmcopy, vmthread, MEMORY_STACK);

  // 堆拷贝并映射
  vmemory_copy_data(vmcopy, vmthread, MEMORY_HEAP);

  // init 0
  if (vmcopy->vma != NULL) {
    vmcopy->vma->alloc_size = 0;
  }

  log_debug("vm clone end\n");
}