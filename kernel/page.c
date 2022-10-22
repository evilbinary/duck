/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "arch/arch.h"
#include "thread.h"

void page_fault(interrupt_context_t *context) {
    
//   u32 fault_addr = cpu_get_fault();
//   int present = context->code & 0x1;
  //   if (present == 0) {
  //     thread_t *current = thread_current();
  //     if (current != NULL) {
  //       vmemory_area_t *area = vmemory_area_find(current->vmm, fault_addr,
  //       0); if (area == NULL) {
  //         if(current->state==THREAD_STOPPED){
  //           return;
  //         }
  //         if (current->fault_count < 3) {
  //           thread_exit(current, -1);
  //           kprintf("tid: %d %s memory fault at %x\n", current->id,
  //           current->name,
  //                   fault_addr);
  //           dump_fault(context, fault_addr);
  //           current->fault_count++;
  //         } else if (current->fault_count == 3) {
  //           kprintf("tid: %d %s memory fault at %x too many\n",
  //                   current->id, current->name, fault_addr);
  //           current->fault_count++;
  //           thread_exit(current, -1);
  //         }
  //         return;
  //       }
  //       void *phy = virtual_to_physic(current->context.page_dir, fault_addr);

  // #ifdef DEBUG_EXCEPTION
  //       kprintf(" tid: %x ", current->id);
  // #endif
  //       if (phy == NULL) {
  //         valloc(fault_addr, PAGE_SIZE);
  //       } else {
  //         kprintf("tid: %d %s phy remap memory fault at %x\n", current->id,
  //                 current->name, fault_addr);
  //         dump_fault(context, fault_addr);
  //         thread_exit(current, -1);
  //       }
  //     } else {
  //       map_page(fault_addr, fault_addr, PAGE_P | PAGE_USU | PAGE_RWW);
  //     }
  //   }

  u32 fault_addr = cpu_get_fault();
  thread_t *current = thread_current();
  if (current != NULL) {
    int mode = context_get_mode(&current->context);
    // kprintf("mode %x\n", mode);
    if (mode == USER_MODE) {
      vmemory_area_t *area = vmemory_area_find(current->vmm, fault_addr, 0);
      if (area == NULL) {
        if (current->fault_count < 3) {
          thread_exit(current, -1);
          kprintf("tid: %d %s memory fault at %x\n", current->id, current->name,
                  fault_addr);
          dump_fault(context, fault_addr);
          current->fault_count++;
        } else if (current->fault_count == 3) {
          kprintf("tid: %d %s memory fault at %x too many\n", current->id,
                  current->name, fault_addr);
          current->fault_count++;
          thread_exit(current, -1);
        }

        return;
      }
      // kprintf("exception at %x\n",page_fault);
      void *phy = virtual_to_physic(current->context.page_dir, fault_addr);
      if (phy == NULL) {
        valloc(fault_addr, PAGE_SIZE);
      } else {
        // valloc(fault_addr, PAGE_SIZE);
        kprintf("tid: %d %s phy: %x remap memory fault at %x\n", current->id,
                current->name, phy, fault_addr);
        dump_fault(context, fault_addr);
        // mmu_dump_page(current->context.page_dir,current->context.page_dir,0);
        thread_exit(current, -1);
        cpu_halt();
      }
    } else {
      kprintf("tid: %d kernel memory fault at %x\n", current->id, fault_addr);
      map_page(fault_addr, fault_addr, PAGE_P | PAGE_USU | PAGE_RWW);
    }
  } else {
    map_page(fault_addr, fault_addr, PAGE_P | PAGE_USU | PAGE_RWW);
  }
}