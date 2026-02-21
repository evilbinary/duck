/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "exceptions.h"

interrupt_handler_t *exception_handlers[EXCEPTION_NUMBER];
void exception_regist(u32 vec, interrupt_handler_t handler) {
  exception_handlers[vec] = handler;
}

void *exception_process(interrupt_context_t *ic) {
  if (ic->no == EX_OTHER) {
    int cpu = cpu_get_id();
    log_debug("exception cpu %d no %d\n", cpu, ic->no);
    thread_t *current = thread_current();
    if (current != NULL) {
      log_debug("tid:%d %s cpu:%d\n", current->id, current->name,
                current->cpu_id);
    }
  } else if (ic->no == EX_SYS_CALL) {
    thread_t *current = thread_current();
    if (current != NULL) {
      current->ctx->ic = ic;
      kmemcpy(current->ctx->ksp, ic, sizeof(interrupt_context_t));
    }
  } else if (ic->no == EX_IRQ) {
    u32 source = interrupt_get_source(ic->no);
    ic->no = source;
  }
  if (exception_handlers[ic->no] != 0) {
    interrupt_handler_t handler = exception_handlers[ic->no];
    if (handler != NULL) {
      void *ret = handler(ic);
      if (ret != NULL) {
        return ret;
      } else {
        return ic;
      }
    }
  } else {
    int cpu = cpu_get_id();
    log_debug("exception hanlder not found on cpu %d no %d\n", cpu, ic->no);
  }
  return NULL;
}

void exception_process_error(thread_t *current, interrupt_context_t *ic,
                             void *entry) {
  thread_exit(current, -1);

  kprintf("--dump interrupt context--\n");
  context_dump_interrupt(ic);
  kprintf("--dump thread--\n");
  thread_dump(current, DUMP_DEFAULT | DUMP_CONTEXT | DUMP_STACK);

  // Point pc at exception_error_exit which lives in the kernel's exec segment.
  // On ARM64 (and any arch with VM), the kernel code segment is mapped into the
  // user page table by page_clone, so EL0 can reach it via on-demand mapping.
  // If the mapping isn't present yet, page_fault_handle will fill it in because
  // the address falls inside a MEMORY_EXEC vmemory_area.
  context_set_entry(ic, entry);
  thread_set_entry(current, entry);
  kprintf("exception process error end\n");
}

// Called in user mode (EL0) after an unrecoverable page fault.
// Issues an exit syscall so the kernel can clean up.
void exception_error_exit() {
  log_debug("exception error exit\n");
  syscall1(1, 555);
  for (;;) {
  }
}

void exception_on_permission(interrupt_context_t *ic) {
  int cpu = cpu_get_id();
  log_debug("exception permission on cpu %d no %d code %x\n", cpu, ic->no,
            ic->code);
  thread_t *current = thread_current();
  if (current != NULL) {
    log_debug("tid:%d %s cpu:%d\n", current->id, current->name,
              current->cpu_id);
  }
  exception_process_error(current, ic, (void *)&exception_error_exit);
  // context_dump_interrupt(ic);
  // thread_dump(current);
  // cpu_halt();
}

void exception_on_other(interrupt_context_t *ic) {
  int cpu = cpu_get_id();
  thread_t *current = thread_current();
  log_debug("exception other on cpu %d no %d code %x\n", cpu, ic->no, ic->code);
  exception_info(ic);
  exception_process_error(current, ic, (void *)&exception_error_exit);
}

void exception_on_undef(interrupt_context_t *ic) {
  int cpu = cpu_get_id();
  thread_t *current = thread_current();
  log_debug("exception undef on cpu %d no %d code %x\n", cpu, ic->no, ic->code);
  exception_info(ic);
  kprintf("--dump thread--\n");
  thread_dump(current, DUMP_DEFAULT | DUMP_CONTEXT);
  exception_process_error(current, ic, (void *)&exception_error_exit);
}

void exception_on_none(interrupt_context_t *ic) {
  
}

void exception_init() {
  interrupt_regist_service(exception_process);

  exception_regist(EX_PERMISSION, exception_on_permission);
  exception_regist(EX_OTHER, exception_on_other);
  exception_regist(EX_PREF_ABORT, exception_on_other);
  exception_regist(EX_RESET, exception_on_other);
  exception_regist(EX_UNDEF, exception_on_undef);
  exception_regist(EX_NONE, exception_on_none);

}
