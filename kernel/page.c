/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "page.h"

#include "page_ref.h"

/* 缺页路径上的调试日志（page fault / page area not found + vmemory_dump /
 * page lookup kernel found phy）量大且逐次缺页都打，默认关闭；排查时打开。 */
#define DEBUG 1

/* 【必须在 page_fault_handle 之前定义】早期内核 fault 分支要读它判断"页表是否
 * 已建好"（NULL ⇒ page_create 期间，映射无处可做 ⇒ 打印后停机，不再无限重试）。 */
void *kernel_page_dir = NULL;

// in user mode
void page_error_exit() {
  log_debug("page erro exit ^_^!!\n");
  syscall1(0, 555);
  for (;;) {
  }
  // never jmp here
}

void* page_fault_handle(interrupt_context_t *ic) {
  vaddr_t fault_addr = (vaddr_t)cpu_get_fault();
  thread_t *current = thread_current();
  static vaddr_t last_fault_addr;
  static u32 last_fault_storm;

  if (current != NULL) {
    current->faults++;
    if (fault_addr == last_fault_addr) {
      last_fault_storm++;
    } else {
      last_fault_addr = fault_addr;
      last_fault_storm = 1;
    }
    if (last_fault_storm > 64) {
      log_error("%s page fault storm at %lx\n", current->name, fault_addr);
      last_fault_storm = 0;
      thread_exit(current, -1);
      exception_process_error(current, ic, (void *)&page_error_exit);
      schedule(ic);
      return ic;
    }

#ifdef DEBUG
    log_debug("page fault at %lx\n", fault_addr);
#endif
    // Check mmap children first (precise range), then fall back to main VMA list.
    // The heap VMA covers the entire heap address range and would otherwise
    // swallow mmap faults before we can check the exact mmap sub-region.
    vmemory_area_t *area = NULL;
    vmemory_area_t *heap = vmemory_area_find_flag(current->vm->vma, MEMORY_HEAP);
    if (heap != NULL && heap->child != NULL) {
      /* size=1 => exact containment: a fault at addr == child->vend must NOT
       * match (the upper bound is inclusive). Previously size=0 matched the
       * top child at the heap's end and the on-demand valloc below mapped a
       * page that no VMA owns. */
      area = vmemory_area_find(heap->child, (void*)fault_addr, 1);
    }
    if (area == NULL) {
      /* Same exact-containment rule: size=1 so a fault at addr == vend does
       * not match the enclosing VMA (upper bound is inclusive in the find). */
      area = vmemory_area_find(current->vm->vma, (void*)fault_addr, 1);
    }
    if (area == NULL) {
      void *phy = page_v2p((u64*)current->vm->kpage, (void*)fault_addr);
#ifdef DEBUG
      log_debug("page area not found %lx\n", fault_addr);
      vmemory_dump(current->vm);
#endif
      if (phy != NULL) {
#ifdef DEBUG
        log_debug("page lookup kernel found phy: %lx\n", phy);
#endif
        /* MMIO/外设常在 EXEC_ADDR 以下 → PAGE_DEV。
         * 高位 FB 别名（T113: 0xfb→0xfe）→ 可缓存 PAGE_FB（与 xwin/gpu 的映射
         * 保持【一致】！同一物理页多映射属性不一致在 ARM 上行为未定义）。
         * 原因：NC 映射下每个像素写直落 DRAM，t113 实测 blit ≈126ms/帧；
         * 改可缓存后由 xwin_flip_buffer() 每帧 clean 一次再让 DE 扫。 */
        /* 【按 VMA/地址区间选属性，不再用 paddr>=0xF0000000 之类的魔数】
         * 走到这里说明没有用户 VMA，是从内核页表按需镜像一个已存在的物理页：
         *   - EXEC_ADDR 以下 = MMIO/外设寄存器 → PAGE_DEV（非缓存）；
         *   - 其余（kuser helper 页 0xffff0000、内核映像等）→ PAGE_USER。
         * FB 有自己的 VMA（MEMORY_FB），不会走这个分支。 */
        u32 attr;
        /* 统一判据（无 #ifdef）：VA < EXEC_ADDR（老平台 MMIO 在低址：raspi3
         * 0x3F300000、t113 0x01C00000…）‖ phys ≥64GB（Pi5 MMIO 直通窗口
         * SDHCI=0x1000FFF000=64GB+16MB、GIC=0x107fff9000=66GB，高于 EXEC_ADDR
         * 使 VA 魔数失效；Pi5 RAM ≤16GB → ≥64GB 必是 MMIO）。
         * 非 Pi5 平台 phys 恒 <64GB（32 位 phy ≤4GB），右条件恒假 ⇒ 与原
         * EXEC_ADDR 判据逐位一致。Pi5 仅剩低址(<1GB) RAM 的异常访问会判 DEV
         * （Device 访 RAM 能完成只是慢，且都在野指针异常路径上）。
         * 属性根源是"从内核页表镜像已验证的映射"；更彻底的统一是直接读内核
         * PTE 的 attr 复用（零魔数），需动 walk 层，暂不做。 */
        attr = (fault_addr < (vaddr_t)EXEC_ADDR ||
                (u64)phy >= 0x1000000000UL)
                   ? PAGE_DEV
                   : PAGE_USER;
        kprintf("pf3 map_on %lx\n", (unsigned long)fault_addr);
        page_map_on((u64*)current->vm->upage, fault_addr, (u64)phy, attr);
        kprintf("pf4 map done v2p=%p\n",
                page_v2p((u64*)current->vm->upage, (void*)fault_addr));
        {
          /* gem_read 看到 1 才打印 pf5。若一直停在 pf4，就是这条 MMIO 读没回来。 */
          extern int gem_pf_trace;
          gem_pf_trace = 1;
        }
      } else {
        if (current->fault_count < 1) {
          log_error("%s memory fault at %lx pc=%lx lr=%lx sp=%lx\n",
                    current->name, (unsigned long)fault_addr,
                    (unsigned long)ic->pc, (unsigned long)ic->lr,
                    (unsigned long)ic->sp);
          /* 【诊断】把崩之前最近的系统调用序列打出来：用户程序崩在野指针
           * （如 miyoo 上 /bin/ls 崩在 dfar=0xfffffffc）时，多半是某个 syscall
           * 返回了负值被当成指针 ⇒ 这串调用号能直接定位是哪一个。 */
          {
            extern void sys_trace_dump(void);
            sys_trace_dump();
          }
          /* vaddr_t：平台无关位宽（aarch64=u64 不截断高地址，32 位=u32），
           * 原遗留 (u32) 会把 0x1000FFF0FC 截成 0xFFF0FC 对齐错位。 */
          void* pte_page = (void*)((vaddr_t)fault_addr & ~(PAGE_SIZE - 1));
          void* pte_prev = (void*)((vaddr_t)pte_page - PAGE_SIZE);
          log_debug("pte %x -> %x\n", pte_page,
                    page_v2p((u64*)current->vm->upage, pte_page));
          log_debug("pte %x -> %x\n", pte_prev,
                    page_v2p((u64*)current->vm->upage, pte_prev));
          thread_exit(current, -1);
          if (current->vm != NULL && current->vm->vma != NULL) {
            for (vmemory_area_t* a = current->vm->vma; a != NULL;
                 a = a->next) {
              log_debug("vma %x-%x flags=%d alloc=%x size=%d child=%p\n",
                        a->vaddr, a->vend, a->flags, a->alloc_addr,
                        a->alloc_size, (void*)a->child);
              for (vmemory_area_t* c = a->child; c != NULL; c = c->next) {
                log_debug("  mmap child %x-%x flags=%d\n", c->vaddr, c->vend,
                          c->flags);
              }
            }
          }
          current->fault_count++;
          exception_process_error(current, ic, (void *)&page_error_exit);
          context_dump_fault(ic, fault_addr);
          thread_dump(current, DUMP_DEFAULT | DUMP_CONTEXT);
          schedule(ic);
        } else if (current->fault_count == 3) {
          log_error("%s memory fault at %lx too many\n", current->name,
                    fault_addr);
          current->fault_count++;
          thread_exit(current, -1);
          exception_process_error(current, ic, (void *)&page_error_exit);
          schedule(ic);
        } else {
          current->fault_count++;
          exception_process_error(current, ic, (void *)&page_error_exit);
          schedule(ic);
        }
      }
      return ic;
    }
    u64 *page = (u64*)current->vm->upage;
    void *phy = page_v2p(page, (void*)fault_addr);

    /* 设备 / 帧缓冲：物理页已存在（内核页表已映射），这里按需镜像到用户页表，
     * 映射属性完全跟着 VMA 类型走：MEMORY_DEV→PAGE_DEV（非缓存），
     * MEMORY_FB→PAGE_FB（可缓存写回）。不再靠地址区间猜。 */
    if (area->flags == MEMORY_DEV || area->flags == MEMORY_FB) {
      u32 dattr = area->attr;
      if (phy == NULL) {
        phy = page_v2p((u64*)current->vm->kpage, (void*)fault_addr);
      }
      if (phy != NULL) {
        page_map_on((u64*)current->vm->upage, fault_addr, (u64)phy, dattr);
      } else {
        log_error("%s dev/fb fault no phy at %lx\n", current->name, fault_addr);
      }
      return ic;
    } else {
      if (phy == NULL) {
        if (area->flags == MEMORY_STACK) {
          extend_stack((void*)fault_addr, PAGE_SIZE);
        } else {
          void* vret = valloc((void*)fault_addr, PAGE_SIZE);
          if (vret == NULL) {
            log_error("page fault valloc failed addr:%lx\n", fault_addr);
            thread_exit(current, -1);
            exception_process_error(current, ic, (void *)&page_error_exit);
            schedule(ic);
          }
        }
      } else {
#ifdef CONFIG_COW
        /* COW：写权限 fault 且命中 fork 共享的只读页 → 复制一份新页。 */
        extern void* mm_alloc_page(void);
        u32 up = (u32)(uintptr_t)current->vm->upage;
        u32 old_pa = 0;
        if (page_fault_is_write() && page_cow_query((u32*)up, fault_addr, &old_pa)) {
          void* np = mm_alloc_page();
          if (np != NULL) {
            u32 new_pa = (u32)(uintptr_t)np;
            kmemcpy((void*)new_pa, (void*)old_pa, PAGE_SIZE);
            page_ref_set(new_pa, 1);
            cpu_flush_dcache_range((unsigned long)new_pa,
                                   (unsigned long)new_pa + PAGE_SIZE);
            page_ref_dec(old_pa);
            page_cow_apply((u32*)up, fault_addr, new_pa);
            return ic;
          }
        }
#endif
        log_error("%s remap memory fault at %lx phy: %lx\n", current->name,
                  fault_addr, phy);
        context_dump_fault(ic, fault_addr);
        thread_exit(current, -1);
        exception_process_error(current, ic, (void *)&page_error_exit);
        schedule(ic);
      }
    }
  } else {
    if (kernel_page_dir == NULL) {
      /* 【早期内核 fault】内核页表还没建好（page_create 期间）就出 kernel
       * fault：page_map 无处可映射，直接返回会让故障指令无限重试刷屏。
       * 把 fault 地址打出来并停机 —— 把"静默死循环"变成一条可诊断信息。 */
      log_error("early kernel fault at %lx (kernel page tables not ready)\n",
                fault_addr);
      cpu_halt();
    }
    page_map(fault_addr, fault_addr, PAGE_KERNEL);
  }
  return ic;
}

void page_map(vaddr_t virtualaddr, vaddr_t physaddr, u32 flags) {
#ifdef VM_ENABLE
  if (kernel_page_dir == NULL) {
    log_error("kernel_page_dir is null\n");
    return;
  }
  page_map_on((u64*)kernel_page_dir, virtualaddr, physaddr, flags);
#endif
}

void page_map_current(vaddr_t virtualaddr, vaddr_t physaddr, u32 flags) {
#ifdef VM_ENABLE
  thread_t* current = thread_current();
  if (current == NULL || current->vm == NULL || current->vm->upage == NULL) {
    return;
  }
  page_map_on((u64*)current->vm->upage, virtualaddr, physaddr, flags);
#endif
}

void *page_kernel_dir() { return kernel_page_dir; }

void page_init() {
#ifdef VM_ENABLE
  int cpu = cpu_get_id();
  if (cpu == 0) {
    exception_regist(EX_DATA_FAULT, page_fault_handle);
    // Bootstrap CPU creates the shared kernel page table.
    kernel_page_dir = page_create(0);
    if (kernel_page_dir == NULL) {
      log_warn("page_create returned NULL, virtual memory not available\n");
      return;
    }

    mm_parse_map(kernel_page_dir);

    log_info("page enable page: %x\n", kernel_page_dir);
    mm_page_enable((page_dir_t)(uintptr_t)kernel_page_dir);
    log_info("page enable end\n");
  } else {
    // Secondary cores only need to attach the existing shared kernel page
    // table locally instead of rebuilding it.
    if (kernel_page_dir == NULL) {
      log_warn("ap %d kernel page dir is null\n", cpu);
      return;
    }

    log_info("ap %d page attach: %x\n", cpu, kernel_page_dir);
    mm_page_enable((page_dir_t)(uintptr_t)kernel_page_dir);
    log_info("ap %d page attach end\n", cpu);
  }
#endif
}
