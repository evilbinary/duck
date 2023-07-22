/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "page.h"

// #define DEBUG 1

// in user mode
void page_error_exit() {
  log_debug("page erro exit ^_^!!\n");
  syscall1(1, 555);
  for (;;) {
  }
  // never jmp here
}

void page_fault_handle(interrupt_context_t *ic) {
  u32 *fault_addr = cpu_get_fault();
  thread_t *current = thread_current();
  if (current != NULL) {
    int mode = context_get_mode(current->ctx);
#ifdef DEBUG
    log_debug("page fault at %x\n", fault_addr);
    // context_dump_fault(ic, fault_addr);
#endif
    vmemory_area_t *area = vmemory_area_find(current->vm->vma, fault_addr, 0);
    if (area == NULL) {
      void *phy = page_v2p(current->vm->kpage, fault_addr);
#ifdef DEBUG
      log_debug("page area not found %x\n", fault_addr);
      vmemory_dump(current->vm);
#endif
      if (phy != NULL) {
#ifdef DEBUG
        log_debug("page lookup kernel found phy: %x\n", phy);
#endif
        // 内核地址，进行映射,todo 进行检查
        page_map_on(current->vm->upage, fault_addr, phy,
                    PAGE_P | PAGE_USR | PAGE_RWX);
      } else {
        if (current->fault_count < 1) {
          thread_exit(current, -1);
          log_error("%s memory fault at %x\n", current->name, fault_addr);
          context_dump_fault(ic, fault_addr);
          thread_dump(current, DUMP_DEFAULT | DUMP_CONTEXT);
          current->fault_count++;
          // cpu_halt();
          exception_process_error(current, ic, (void *)&page_error_exit);
          schedule(ic);
        } else if (current->fault_count == 3) {
          log_error("%s memory fault at %x too many\n", current->name,
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
      return;
    }
    u32 *page = current->vm->upage;
    void *phy = page_v2p(page, fault_addr);
    if (phy == NULL) {
      if (area->flags == MEMORY_STACK) {
        extend_stack(fault_addr, PAGE_SIZE);
      } else {
        valloc(fault_addr, PAGE_SIZE);
      }

    } else {
      log_error("%s remap memory fault at %x phy: %x\n", current->name,
                fault_addr, phy);
      // mmu_dump_page(page, page, 0);
      context_dump_fault(ic, fault_addr);
      thread_exit(current, -1);
      exception_process_error(current, ic, (void *)&page_error_exit);
      schedule(ic);
    }
  } else {
    page_map(fault_addr, fault_addr, PAGE_P | PAGE_USR | PAGE_RWX);
  }
}

void *kernel_page_dir = NULL;

void page_map(u32 virtualaddr, u32 physaddr, u32 flags) {
  if (kernel_page_dir == NULL) {
    log_error("kernel_page_dir is null\n");
    return;
  }
  page_map_on(kernel_page_dir, virtualaddr, physaddr, flags);
}

void *page_kernel_dir() { return kernel_page_dir; }

void page_init() {
  exception_regist(EX_DATA_FAULT, page_fault_handle);

#ifdef VM_ENABLE
  // create kernel page
  kernel_page_dir = page_create(0);
  // parse
  mm_parse_map(kernel_page_dir);

  // enable page
  log_info("page enable page: %x\n", kernel_page_dir);
  mm_enable(kernel_page_dir);
  log_info("page enable end\n");
#endif
}
