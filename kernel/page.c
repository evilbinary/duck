/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "page.h"

#include "memory.h" /* memfd_fault_hook（见 memory.h 钩子说明） */
#include "page_ref.h"
#include "vfs.h"

/* 缺页路径上的调试日志（page fault / page area not found + vmemory_dump /
 * page lookup kernel found phy）量大且逐次缺页都打，默认关闭；排查时打开。
 * 关掉的原因：跑大模型时每页缺页打一条，810MB 就是 20 万行串口输出，
 * 光是打印就能把系统拖死。
 * 【注意】下面用的是 #ifdef DEBUG，写 `#define DEBUG 0` 是【无效】的
 * （只要定义了就成立）—— 关闭必须把这一行注释掉，打开就取消注释。 */
/* #define DEBUG 1 */

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
        page_map_on((u64*)current->vm->upage, fault_addr, (u64)phy, attr);
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
          /* 【诊断必须在 thread_exit 之前】thread_exit 会把线程 stop 并从
           * 调度链摘除——之后 thread_current() 不再返回它，后续 dump/
           * exception 处理里任何一次取 current 都可能拿到 NULL/别的线程，
           * 再 fault 就是 handler 里 addr=0 递归 fault（实测 clone04 用户态
           * 崩在 musl __clone 的 NULL stack → 内核挂死在 bt_dump）。 */
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
          context_dump_fault(ic, fault_addr);
          thread_dump(current, DUMP_DEFAULT | DUMP_CONTEXT);
          current->fault_count++;
          /* 【不再走 exception_process_error】它内部 fault_hook(bt_dump 切栈)
           * + thread_exit + set_entry 三件套在"杀已停止线程"场景会互相踩
           * （双重 thread_exit 损坏回收链、bt_dump 专用栈切换遇到残破 SP 后
           * 嵌套异常无限循环，实测 clone04 用户态崩 → 内核假死）。杀进程
           * 只需要 thread_exit + schedule，诊断已全部在上方完成。 */
          thread_exit(current, -1);
          schedule(ic);
        } else if (current->fault_count == 3) {
          log_error("%s memory fault at %lx too many\n", current->name,
                    fault_addr);
          current->fault_count++;
          thread_exit(current, -1);
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
        /* 【swap 换入】该页之前被换出过：先查记录，命中就换回。
         * 必须放在 stack/file/anon 各分支之前——被换出的页没有任何其它
         * 补页路径（file_node 是 NULL 的匿名页会补 0，数据就丢了）。
         * valloc 内部带"页池耗尽 → swap 回收再试"，换入自身页池不足时
         * 会换出别的页腾地方。 */
        {
          vaddr_t pva = (vaddr_t)fault_addr & ~((vaddr_t)PAGE_SIZE - 1);
          u32 slot = page_swap_lookup(current->vm, pva);
          if (slot != 0) {
            void* vret = valloc((void*)pva, PAGE_SIZE);
            if (vret != NULL && page_swap_read_page(slot, (void*)pva) == 0) {
              page_swap_forget(current->vm, pva);
              return ic;
            }
            /* 换入失败：保留记录，落到下面的正常分支（大概率再次失败并
             * 走线程退出，与缺页无法满足时的既有行为一致） */
          }
        }
        if (area->flags == MEMORY_STACK) {
          extend_stack((void*)fault_addr, PAGE_SIZE);
        } else if (area->file_node != NULL) {
          /* 【文件映射·按需补页】mmap 只登记了 VMA（sysfn.c 的 lazy 路径），
           * 第一次访问到这里才分配物理页并从文件读入对应页内容。
           * 810MB 模型映射不再一次性吃光页池——只有真正被访问的页占内存。
           * 读不到的部分（超过文件尾/读到已关闭的 FIL）保持 0，与旧的全量
           * kmemset+vread 行为一致。vread 的偏移是 u32：模型 810MB < 4GB 够用。
           *
           * 【必须按页对齐】fault_addr 是触发访问的原始地址，落在页中间是常态
           * （如 p[PAGE+123] 的首次写）。旧代码直接用它算 foff 并当补页缓冲区：
           *   1) foff 未对齐 ⇒ 从文件读到的是错位内容；
           *   2) kmemset/vread 从页中间写满 4KB ⇒ 越过本页尾、踩下一页
           *      （映射末尾那页还会写到 VMA 之外）。
           * test-mmap-file 的 test_mmap_file_write 就是在这里露的：
           * 页首 123 字节既没被补、也没被清（valloc 清零）⇒ 读到 0。 */
          vnode_t* node = (vnode_t*)area->file_node;
          vaddr_t page_delta =
              (vaddr_t)fault_addr & ((vaddr_t)PAGE_SIZE - 1);
          vaddr_t page_va = (vaddr_t)fault_addr - page_delta;
          /* 【共享页节点（memfd 等）：直接映射物理页】Linux 语义是
           * "同一 inode 的页"而不是"文件映射按需读"。节点实现了
           * op->fault 就把它返回的页装配进当前进程页表（ref++ 防另一
           * 进程退出时被回收），多进程看到同一份物理页；
           * 越界/普通文件落到下面文件读取路径。 */
          if (node->op != NULL && node->op->fault != NULL) {
            void* pa = node->op->fault(
                node, area->file_off + (u64)(page_va - area->vaddr));
            if (pa != NULL) {
              page_ref_inc((u32)(uintptr_t)pa);
              page_map_on((u64*)current->vm->upage, (void*)page_va, (u64)pa,
                          PAGE_USER);
              return ic;
            }
          }
          /* 【块填充】单页缺页 ⇒ 按 256KB 对齐块补：一次 valloc + 一次
           * vread 顶 64 次单页往返。推理顺序触达权重，块内页迟早全用，
           * 无浪费；超出文件尾保持 0（vread 短读，与单页行为一致）。
           * 772MB 模型 = 19.8 万次缺页，QEMU 下每次 fatfs 4KB 读几十 ms，
           * 按页补要半小时级；按块补 fatfs 调用数降 64 倍。 */
          vaddr_t fill_va = page_va;
          u32 fill_len = PAGE_SIZE;
          int chunk_ok = 0;
          {
            const vaddr_t CHUNK = 256 * 1024;
            vaddr_t cstart = page_va & ~(CHUNK - 1);
            vaddr_t cend = cstart + CHUNK;
            if (cstart < area->vaddr) {
              cstart = area->vaddr;
            }
            if (cend > area->vend) {
              cend = area->vend;
            }
            /* 块内有已映射页（此前单页回退填充过）就别重复 valloc，
             * 直接走单页路径 */
            vaddr_t p;
            int mapped = 0;
            for (p = cstart; p < cend; p += PAGE_SIZE) {
              if (page_v2p(current->vm->upage, (void*)p) != NULL) {
                mapped = 1;
                break;
              }
            }
            if (!mapped && valloc_nozero((void*)cstart, cend - cstart) != NULL) {
              fill_va = cstart;
              fill_len = (u32)(cend - cstart);
              chunk_ok = 1;
            }
          }
          u64 foff = area->file_off + (u64)(fill_va - area->vaddr);
          /* 块已 valloc 成功就直接用；否则（含块被截成一页的情形）单页分配 */
          void* vret = chunk_ok ? (void*)fill_va
                                : valloc((void*)fill_va, PAGE_SIZE);
          if (vret == NULL) {
            log_error("file map valloc failed addr:%lx\n",
                      (unsigned long)page_va);
            thread_exit(current, -1);
            exception_process_error(current, ic, (void*)&page_error_exit);
            schedule(ic);
          } else {
            /* 【尾部清零】valloc_nozero 的页是池里旧数据：vread 覆盖到哪
             * 算哪，剩余尾部（短读/EOF/读错）必须清零，语义与全量
             * kmemset+vread 一致。 */
            u32 page_off = 0;
            while (page_off < fill_len) {
              u32 done = vread(node, (u32)(foff + page_off),
                               fill_len - page_off,
                               (u8*)fill_va + page_off);
              if (done == 0 || done == (u32)-1) {
                break;
              }
              page_off += done;
            }
            if (page_off < fill_len) {
              kmemset((u8*)fill_va + page_off, 0, fill_len - page_off);
            }
          }
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
          if (np == NULL) {
            /* 页池耗尽：换出几页再试。skip_pa=源页——它马上要被拷贝，
             * 绝不能被回收（否则拷到已释放页 = 数据损坏）。 */
            if (page_swap_reclaim(4, old_pa) > 0) {
              np = mm_alloc_page();
            }
          }
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
    /* 【NULL current 安全阀】fault 发生时没有线程上下文（tid=-1，如死线程
     * 退出/回收路径里的未知异常 code=0 addr=0）：这种 fault 不可能靠映射
     * 修好，page_map 后返回会让同一指令无限重试 = QEMU 满载假死。非内核
     * VA 一律停机并打出完整现场。 */
    if (fault_addr < 0x80000000UL || ic == NULL) {
      log_error("fault with no thread context: addr=%lx pc=%lx lr=%lx sp=%lx "
                "no=%d code=%d\n",
                (unsigned long)fault_addr, ic != NULL ? (unsigned long)ic->pc : 0,
                ic != NULL ? (unsigned long)ic->lr : 0,
                ic != NULL ? (unsigned long)ic->sp : 0,
                ic != NULL ? ic->no : -1, ic != NULL ? ic->code : -1);
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

/* ---- 页交换 hook：默认 no-op，swap 模块 init 时注册真实现 ----
 * 内核侧只有指针，没有对 modules/swap 的头文件/符号依赖。 */
static const page_swap_ops_t* page_swap_ops;

void page_swap_ops_register(const page_swap_ops_t* ops) {
  if (ops == NULL || ops->lookup == NULL || ops->forget == NULL ||
      ops->drop_vm == NULL || ops->reclaim == NULL || ops->read_page == NULL ||
      ops->swapback_vm == NULL) {
    log_error("page swap ops invalid, ignore\n");
    return;
  }
  page_swap_ops = ops;
  log_info("page swap ops registered\n");
}

u32 page_swap_lookup(void* vm, vaddr_t va) {
  return (page_swap_ops != NULL) ? page_swap_ops->lookup(vm, va) : 0;
}

void page_swap_forget(void* vm, vaddr_t va) {
  if (page_swap_ops != NULL) {
    page_swap_ops->forget(vm, va);
  }
}

void page_swap_drop_vm(void* vm) {
  if (page_swap_ops != NULL) {
    page_swap_ops->drop_vm(vm);
  }
}

int page_swap_reclaim(u32 npages, u32 skip_pa) {
  return (page_swap_ops != NULL) ? page_swap_ops->reclaim(npages, skip_pa) : 0;
}

int page_swap_read_page(u32 slot, void* page) {
  return (page_swap_ops != NULL) ? page_swap_ops->read_page(slot, page) : -1;
}

int page_swap_swapback_vm(void* vm) {
  return (page_swap_ops != NULL) ? page_swap_ops->swapback_vm(vm) : 0;
}

void page_swap_stat(u64* total_bytes, u64* used_bytes) {
  if (total_bytes != NULL) {
    *total_bytes = 0;
  }
  if (used_bytes != NULL) {
    *used_bytes = 0;
  }
  if (page_swap_ops != NULL && page_swap_ops->stat != NULL) {
    page_swap_ops->stat(total_bytes, used_bytes);
  }
}

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
