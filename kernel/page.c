/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "page.h"

// #define DEBUG 1

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

    int mode = context_get_mode(current->ctx);
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
         * 高位 FB 别名（T113: 0xfb→0xfe）→ PAGE_RW_NC，与 gpu 映射一致。 */
        u32 attr;
        u32 paddr = (u32)(uintptr_t)phy;
        if (fault_addr < (vaddr_t)EXEC_ADDR) {
          attr = PAGE_DEV;
        } else if (paddr >= 0xF0000000u ||
                   (u32)(uintptr_t)fault_addr >= 0xF0000000u) {
          attr = PAGE_RW_NC;
        } else {
          attr = PAGE_P | PAGE_USR | PAGE_RWX;
        }
        page_map_on((u64*)current->vm->upage, fault_addr, (u64)phy, attr);
      } else {
        if (current->fault_count < 1) {
          log_error("%s memory fault at %lx\n", current->name, fault_addr);
          void* pte_page = (void*)((u32)fault_addr & ~(PAGE_SIZE - 1));
          void* pte_prev = (void*)((u32)pte_page - PAGE_SIZE);
          log_error("pte %x -> %x\n", pte_page,
                    page_v2p((u64*)current->vm->upage, pte_page));
          log_error("pte %x -> %x\n", pte_prev,
                    page_v2p((u64*)current->vm->upage, pte_prev));
          thread_exit(current, -1);
          if (current->vm != NULL && current->vm->vma != NULL) {
            for (vmemory_area_t* a = current->vm->vma; a != NULL;
                 a = a->next) {
              log_error("vma %x-%x flags=%d alloc=%x size=%d child=%p\n",
                        a->vaddr, a->vend, a->flags, a->alloc_addr,
                        a->alloc_size, (void*)a->child);
              for (vmemory_area_t* c = a->child; c != NULL; c = c->next) {
                log_error("  mmap child %x-%x flags=%d\n", c->vaddr, c->vend,
                          c->flags);
              }
            }
          }
          context_dump_fault(ic, fault_addr);
          thread_dump(current, DUMP_DEFAULT | DUMP_CONTEXT);
          current->fault_count++;
          exception_process_error(current, ic, (void *)&page_error_exit);
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

    if (area->flags == MEMORY_DEV) {
      if (phy == NULL) {
        phy = page_v2p((u64*)current->vm->kpage, (void*)fault_addr);
      }
      if (phy != NULL) {
        page_map_on((u64*)current->vm->upage, fault_addr, (u64)phy, PAGE_DEV);
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
        log_error("%s remap memory fault at %lx phy: %lx\n", current->name,
                  fault_addr, phy);
        context_dump_fault(ic, fault_addr);
        thread_exit(current, -1);
        exception_process_error(current, ic, (void *)&page_error_exit);
        schedule(ic);
      }
    }
  } else {
    page_map(fault_addr, fault_addr, PAGE_P | PAGE_USR | PAGE_RWX);
  }
  return ic;
}

void *kernel_page_dir = NULL;

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
