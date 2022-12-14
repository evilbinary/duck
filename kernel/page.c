/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "page.h"

// #define DEBUG

// in user mode
void page_error_exit() {
  syscall1(SYS_PRINT, "page erro exit\n");
  syscall1(SYS_EXIT, 666);
  cpu_halt();
}

void page_fault_handle(interrupt_context_t *ic) {
  u32 *fault_addr = cpu_get_fault();
  thread_t *current = thread_current();
  if (current != NULL) {
    int mode = context_get_mode(&current->context);
#ifdef DEBUG
    log_debug("page fault at %x\n", fault_addr);
#endif
    vmemory_area_t *area = vmemory_area_find(current->vmm, fault_addr, 0);
    if (area == NULL) {
      void *phy = virtual_to_physic(current->context.kpage, fault_addr);
#ifdef DEBUG
      log_debug("page area not found %x\n", fault_addr);
#endif
      if (phy != NULL) {
#ifdef DEBUG
        log_debug("page lookup kernel found phy: %x\n", phy);
#endif
        // 内核地址，进行映射,todo 进行检查
        map_page_on(current->context.upage, fault_addr, phy,
                    PAGE_P | PAGE_USU | PAGE_RWW);
      } else {
        if (current->fault_count < 1) {
          thread_exit(current, -1);
          log_error("%s memory fault at %x\n", current->name, fault_addr);
          context_dump_fault(ic, fault_addr);
          thread_dump(current, DUMP_DEFAULT |DUMP_CONTEXT);
          current->fault_count++;
          // cpu_halt();
        } else if (current->fault_count == 3) {
          log_error("%s memory fault at %x too many\n", current->name,
                    fault_addr);
          current->fault_count++;
          thread_exit(current, -1);

          exception_process_error(current, ic, (void *)&page_error_exit);
        } else {
          current->fault_count++;
          exception_process_error(current, ic, (void *)&page_error_exit);
        }
      }
      return;
    }
    // kprintf("exception at %x\n",page_fault);
    void *phy = virtual_to_physic(current->context.upage, fault_addr);
    if (phy == NULL) {
      valloc(fault_addr, PAGE_SIZE);
    } else {
      // valloc(fault_addr, PAGE_SIZE);
      log_error("%s phy: %x remap memory fault at %x\n", current->name, phy,
                fault_addr);
      context_dump_fault(ic, fault_addr);
      // mmu_dump_page(current->context.upage,current->context.upage,0);
      thread_exit(current, -1);
      exception_process_error(current, ic, (void *)&page_error_exit);
    }
  } else {
    map_page(fault_addr, fault_addr, PAGE_P | PAGE_USU | PAGE_RWW);
  }
}

void page_init() { exception_regist(EX_DATA_FAULT, page_fault_handle); }
