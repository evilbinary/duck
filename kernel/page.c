/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "page.h"

void page_fault_handle(interrupt_context_t *context) {
  u32 *fault_addr = cpu_get_fault();
  thread_t *current = thread_current();
  if (current != NULL) {
    int mode = context_get_mode(&current->context);
    log_debug("page fault at %x\n",fault_addr);
    if(fault_addr == 0x8000000 || fault_addr==0x200000 ||fault_addr<=0x24 ){
      int i=0;
    }
    vmemory_area_t *area = vmemory_area_find(current->vmm, fault_addr, 0);
    if (area == NULL) {
      void *phy =
          virtual_to_physic(current->context.kernel_page_dir, fault_addr);
      if (phy != NULL) {
        //内核地址，进行映射,todo 进行检查
        map_page_on(current->context.page_dir, fault_addr, phy,
                    PAGE_P | PAGE_USU | PAGE_RWW);
      } else {
        if (current->fault_count < 1) {
          thread_exit(current, -1);
          log_error("%s memory fault at %x\n", current->name, fault_addr);
          context_dump_fault(context, fault_addr);
          current->fault_count++;
        } else if (current->fault_count == 3) {
          log_error("%s memory fault at %x too many\n", current->name,
                    fault_addr);
          current->fault_count++;
          thread_exit(current, -1);
        } else {
          current->fault_count++;
        }
      }
      return;
    }
    // kprintf("exception at %x\n",page_fault);
    void *phy = virtual_to_physic(current->context.page_dir, fault_addr);
    if (phy == NULL) {
      valloc(fault_addr, PAGE_SIZE);
    } else {
      // valloc(fault_addr, PAGE_SIZE);
      log_error("%s phy: %x remap memory fault at %x\n", current->name, phy,
                fault_addr);
      context_dump_fault(context, fault_addr);
      // mmu_dump_page(current->context.page_dir,current->context.page_dir,0);
      thread_exit(current, -1);
      cpu_halt();
    }
  } else {
    map_page(fault_addr, fault_addr, PAGE_P | PAGE_USU | PAGE_RWW);
  }
}

void page_init() {}
