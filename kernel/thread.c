/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "thread.h"

#include "fd.h"
#include "loader.h"
#include "syscall.h"

thread_t* current_threads[MAX_CPU] = {0};
thread_t* schedulable_head_thread[MAX_CPU] = {0};
thread_t* schedulable_tail_thread[MAX_CPU] = {0};

thread_t* recycle_head_thread = NULL;
thread_t* recycle_tail_thread = NULL;
u32 recycle_head_thread_count = 0;

u32 thread_ids = 0;
lock_t thread_lock;

#define DEBUG 1

void thread_init() { lock_init(&thread_lock); }

thread_t* thread_create_level(void* entry, void* data, u32 level) {
  thread_t* thread =
      thread_create_ex(entry, KERNEL_THREAD_STACK_SIZE, THREAD_STACK_SIZE, data,
                       level, THREAD_NEW);
  return thread;
}

thread_t* thread_create_name(char* name, void* entry, void* data) {
  thread_t* t = thread_create(entry, data);
  if (t == NULL) return t;
  t->name = name;
  return t;
}

thread_t* thread_create_name_level(char* name, void* entry, void* data,
                                   u32 level) {
  thread_t* t = thread_create_level(entry, data, level);
  if (t == NULL) return t;
  t->name = name;
  return t;
}

thread_t* thread_create(void* entry, void* data) {
  return thread_create_level(entry, data, USER_MODE);
}

thread_t* thread_create_ex_name(char* name, void* entry, u32 size, void* data,
                                u32 level, u32 flags) {
  thread_t* t = thread_create_ex(entry, KERNEL_THREAD_STACK_SIZE, size, data,
                                 level, flags);
  if (t == NULL) return t;
  char* kname = kmalloc(kstrlen(name), KERNEL_TYPE);
  kstrcpy(kname, name);
  t->name = kname;
  return t;
}

void thread_init_default(thread_t* thread, u32 level, u32* entry, void* data) {
  thread->id = thread_ids++;
  thread->lock = 0;
  thread->next = NULL;
  thread->priority = 1;
  thread->counter = 0;
  thread->sleep_counter = 0;
  thread->state = THREAD_CREATE;
  thread->level = level;
  thread->cpu_id = cpu_get_id();
  thread->fd_size = 40;
  thread->fd_number = 0;
  thread->data = data;
  thread->mem = 0;
}

thread_t* thread_create_ex(void* entry, u32 kstack_size, u32 ustack_size,
                           void* data, u32 level, u32 flags) {
  if (ustack_size <= 0) {
    log_error("thread create ex  user stack size is 0\n");
    return NULL;
  }
  thread_t* thread = kmalloc(sizeof(thread_t), KERNEL_TYPE);
  thread_init_default(thread, level, entry, data);

  thread->fds = kmalloc(sizeof(fd_t) * thread->fd_size, KERNEL_TYPE);

  // context init
  context_t* ctx = kmalloc(sizeof(context_t), KERNEL_TYPE);
  thread->ctx = ctx;
  ctx->tid = thread->id;

  u32 ksp = kmalloc_alignment(kstack_size, PAGE_SIZE, KERNEL_TYPE);
  u32 usp = kmalloc_alignment(ustack_size, PAGE_SIZE, KERNEL_TYPE);
  ctx->ksp_start = ksp;
  ctx->ksp_end = ksp + kstack_size;
  ctx->ksp_size = kstack_size;
  ctx->usp = usp + ustack_size;
  ctx->usp_size = ustack_size;

  context_init(ctx, ctx->ksp_end, ctx->usp, entry, thread->level,
               thread->cpu_id);

  // vm init
  vmemory_t* vm = kmalloc(sizeof(vmemory_t), KERNEL_TYPE);
  thread->vm = vm;
  vm->tid=thread->id;
  // init vm include stack heap exec
  vmemory_init(vm, level, usp, ustack_size, flags);

#ifdef VM_ENABLE
  vmemory_area_t* vm_stack = vmemory_area_find_flag(vm->vma, MEMORY_STACK);
  context_init(ctx, ctx->ksp_end, vm_stack->vend, entry, thread->level,
               thread->cpu_id);
#endif

  // vfs
  thread->vfs = kmalloc(sizeof(vfs_t), KERNEL_TYPE);
  // file description
  thread_fill_fd(thread);

  // check thread data
  int ret = thread_check(thread);
  return thread;
}

thread_t* thread_copy(thread_t* thread, u32 flags) {
  if (thread == NULL) {
    return NULL;
  }
  log_debug("thread copy start\n");

  thread_t* copy = kmalloc(sizeof(thread_t), KERNEL_TYPE);

  kmemmove(copy, thread, sizeof(thread_t));

  log_debug("thread init default\n");

  thread_init_default(copy, thread->level, thread->ctx->eip, thread->data);
  copy->data = thread->data;
  copy->pid = thread->id;
  copy->name = kmalloc(kstrlen(thread->name), KERNEL_TYPE);
  kstrcpy(copy->name, thread->name);
  copy->counter = 0;
  copy->fault_count = 0;
  copy->sleep_counter = 0;
  copy->dump_count=0;

  // context init
  context_t* ctx = kmalloc(sizeof(context_t), KERNEL_TYPE);
  copy->ctx = ctx;
  ctx->tid = copy->id;

  u32 kstack_size = thread->ctx->ksp_size;
  u32 ustack_size = thread->ctx->usp_size;

  u32 ksp = kmalloc(kstack_size, KERNEL_TYPE);
  ctx->ksp_start = ksp;
  ctx->ksp_end = ksp + kstack_size;
  ctx->ksp_size = kstack_size;

  ctx->usp_size = ustack_size;

  context_clone(copy->ctx, thread->ctx);

  // vm init
  copy->vm = kmalloc(sizeof(vmemory_t), KERNEL_TYPE);
  copy->vm->tid=copy->id;

  // init vm include stack heap exec
  vmemory_clone(copy->vm, thread->vm, flags);

  // 文件分配方式
  if (flags & FS_CLONE) {
    // copy file
    copy->fd_size = thread->fd_size;
    copy->fd_number = thread->fd_number;
    copy->fds = kmalloc(sizeof(fd_t) * thread->fd_size, KERNEL_TYPE);
    kmemmove(copy->fds, thread->fds, sizeof(fd_t) * thread->fd_size);
  }

  // check thread data
  int ret = thread_check(copy);
  log_debug("thread copy end\n");
  return copy;
}

int thread_check(thread_t* thread) {
  if (thread->ctx->ksp_start == NULL) {
    log_error("create thread %d faild for ksp start is null\n", thread->id);
    return -1;
  }
  if (thread->ctx->ksp_end == NULL) {
    log_error("create thread %d faild for ksp end is null\n", thread->id);
    return -1;
  }
  if (thread->ctx->ksp_size <= 0) {
    log_error("create thread %d faild for ksp size is 0\n", thread->id);
    return -1;
  }
  if ((thread->ctx->ksp_end - thread->ctx->ksp_start) !=
      thread->ctx->ksp_size) {
    log_error("create thread %d faild for ksp size is not equal\n", thread->id);
    return -1;
  }

  if (thread->vm->upage == NULL) {
    log_error("create thread %d faild for upage is null\n", thread->id);
    return -1;
  }

  if (thread->ctx->usp_size <= 0) {
    log_error("create thread %d faild for ustack size is 0\n", thread->id);
    return -1;
  }
  if (thread->ctx->usp <= 0) {
    log_error("create thread %d faild for ustack is 0\n", thread->id);
    return -1;
  }
  if (thread->vm->kpage == NULL) {
    log_error("create thread %d faild for kpage is null\n", thread->id);
    return -1;
  }
  if (thread->vm->upage == NULL) {
    log_error("create thread %d faild for upage is null\n", thread->id);
    return -1;
  }
  if (thread->vm->vma == NULL) {
    log_error("create thread %d faild for vma is null\n", thread->id);
    return -1;
  }

  // check stack map
  vmemory_area_t* vm_stack =
      vmemory_area_find_flag(thread->vm->vma, MEMORY_STACK);
  if (vm_stack == NULL) {
    log_error("create thread %d faild for stack is null\n", thread->id);
    return -1;
  }

  if (thread->ctx->usp < vm_stack->vaddr || thread->ctx->usp > vm_stack->vend) {
    log_error("create thread %d faild for ustack %x range [%x - %x] error\n",
              thread->id, thread->ctx->usp, vm_stack->vaddr, vm_stack->vend);
    return -1;
  }

  void* phy = page_v2p(thread->vm->upage, vm_stack->alloc_addr);
  if (phy == NULL) {
    log_error("thread map have error\n");
    return -1;
  }
  vmemory_dump(thread->vm);
  thread_dump(thread,0);

  log_debug("tid %d kpage %x upage %x\n", thread->id, thread->vm->kpage,
            thread->vm->upage);
  log_debug("tid %d ksp %x usp %x\n", thread->id, thread->ctx->ksp,
            thread->ctx->usp);

  return 0;
}

void thread_fill_fd(thread_t* thread) {
  thread->fds[STDIN] = fd_find(STDIN);
  thread->fds[STDOUT] = fd_find(STDOUT);
  thread->fds[STDERR] = fd_find(STDERR);
  // thread->fds[STDSELF]=3;
  for (int i = STDIN; i <= STDERR; i++) {
    if (thread->fds[STDIN] != NULL) {
      thread->fd_number++;
    }
  }
}

void thread_sleep(thread_t* thread, u32 count) {
  thread->state = THREAD_SLEEP;
  thread->sleep_counter = count;
}

void thread_wait(thread_t* thread) {
#ifdef DEBUG_THREAD
  log_debug("thread %d wait==============> %d\n", current_threads[cpu_id]->id,
            thread->id);
#endif
  thread->state = THREAD_WAITING;
  schedule_next();
}

void thread_wake(thread_t* thread) {
#ifdef DEBUG_THREAD
  log_debug("thread %d wake==============> %d\n", current_threads[cpu_id]->id,
            thread->id);
#endif
  thread->state = THREAD_RUNNING;
  thread->sleep_counter = 0;
  schedule_next();
}

void thread_set_entry(thread_t* thread, void* entry) {
  if (thread == NULL) return;
  interrupt_context_t* ic = thread->ctx->ksp;
  if (ic == NULL) {
    log_error("context is null cannot set ret\n");
    return;
  }
  context_set_entry(ic, entry);
}

void thread_set_arg(thread_t* thread, void* arg) {
  if (thread == NULL) return;
  interrupt_context_t* ic = thread->ctx->ksp;
  if (ic == NULL) {
    log_error("context is null cannot set ret\n");
    return;
  }
  context_ret(ic) = arg;
}

void thread_set_ret(thread_t* thread, u32 ret) {
  if (thread == NULL) return;
  interrupt_context_t* ic = thread->ctx->ksp;
  if (ic == NULL) {
    log_error("context is null cannot set ret\n");
    return;
  }
  context_ret(ic) = 0;
}

void thread_set_params(thread_t* thread, void* args, int size) {
  if (thread == NULL) return;
  // todo copy
}

void thread_reset_user_stack(thread_t* thread, u32* ustack) {
  // todo
}

void thread_add(thread_t* thread) {
  // lock_acquire(&thread_lock);

  // 内核需要物理地址
  int cpu_id = cpu_get_id();
  if (schedulable_head_thread[cpu_id] == NULL) {
    schedulable_head_thread[cpu_id] = thread;
    schedulable_tail_thread[cpu_id] = thread;
  } else {
    schedulable_tail_thread[cpu_id]->next = thread;
    schedulable_tail_thread[cpu_id] = thread;
  }
  thread->state = THREAD_RUNABLE;
  if (current_threads[cpu_id] == NULL) {
    if (schedulable_head_thread[cpu_id] == NULL) {
      log_error("no thread please create a thread\n");
      cpu_halt();
    }
    current_threads[cpu_id] = schedulable_head_thread[cpu_id];
    // current_context = &current_threads[cpu_id]->context;
  }
  // lock_release(&thread_lock);
}

void thread_remove(thread_t* thread) {
  // lock_acquire(&thread_lock);
  int cpu_id = cpu_get_id();
  thread_t* prev = schedulable_head_thread[cpu_id];
  thread_t* v = prev->next;
  thread->state = THREAD_STOPPED;
  thread->counter += 1000;

  if (schedulable_head_thread[cpu_id] == thread) {
    schedulable_head_thread[cpu_id] = NULL;
    schedulable_tail_thread[cpu_id] = NULL;
    thread->next = NULL;
    // lock_release(&thread_lock);

    return;
  }

  for (; v; v = v->next) {
    if (v == thread) {
      prev->next = v->next;
      v->next = NULL;
      if (thread == schedulable_tail_thread[cpu_id]) {
        schedulable_tail_thread[cpu_id] = prev;
      }
      break;
    }
    prev = v;
  }
  // lock_release(&thread_lock);
}

void thread_destroy(thread_t* thread) {
  if (thread == NULL) return;
  if (thread->ctx->ksp != NULL) {
    // kfree(thread->ctx->ksp);
  }
  if (thread->ctx->usp != NULL) {
    // kfree(thread->ctx->usp);
  }
  kfree(thread);
}

void thread_recycle(thread_t* thread) {
  thread_remove(thread);
  // add into cycle thread
  if (recycle_head_thread == NULL) {
    recycle_head_thread = thread;
    recycle_tail_thread = thread;
  } else {
    recycle_tail_thread->next = thread;
    recycle_tail_thread = thread;
  }
  recycle_head_thread_count++;
  // todo free page alloc
  // page_free(thread->vm->upage, thread->level);
}

void thread_stop(thread_t* thread) {
  if (thread == NULL) return;
  thread->state = THREAD_STOPPED;
  // thread_recycle(thread);
  // kprintf("recycle count %d\n", recycle_head_thread_count);
  // schedule_next();
  // cpu_sti();
}

thread_t* thread_head() { return schedulable_head_thread[cpu_get_id()]; }

void thread_exit(thread_t* thread, int code) {
  if (thread == NULL) return;
  thread->code = code;
  thread_stop(thread);
}

thread_t* thread_find_next(thread_t* thread) {
  thread_t* v = schedulable_head_thread[cpu_get_id()];

  for (; v; v = v->next) {
    if (v->next == thread) {
      return v;
    }
  }
  return NULL;
}

void thread_run(thread_t* thread) {
  if (thread->state == THREAD_CREATE) {
    thread_add(thread);
  }
  if (thread->state == THREAD_RUNABLE) {
    thread->state = THREAD_RUNNING;
  } else if (thread->state == THREAD_STOPPED) {
    thread->state = THREAD_RUNNING;
  }
}

void thread_yield() {
  thread_t* current = thread_current();
  if (current == NULL) {
    return;
  }
  if (current->state == THREAD_RUNNING) {
    current->counter++;
    schedule_next();
  }
}

thread_t* thread_current() {
  // lock_acquire(&thread_lock);
  int cpu_id = cpu_get_id();
  thread_t* t = current_threads[cpu_id];
  // lock_release(&thread_lock);
  return t;
}

void thread_set_current(thread_t* thread) {
  // lock_acquire(&thread_lock);
  int cpu_id = cpu_get_id();
  current_threads[cpu_id] = thread;
  // lock_release(&thread_lock);
}

context_t* thread_current_context() {
  // lock_acquire(&thread_lock);
  int cpu_id = cpu_get_id();
  thread_t* t = current_threads[cpu_id];
  // lock_release(&thread_lock);
  return t->ctx;
}

int thread_find_fd_name(thread_t* thread, u8* name) {
  if (thread->fd_number == 0) {
    thread_fill_fd(thread);
  }
  if (thread->fd_number > thread->fd_size) {
    log_error("thread find fd name limit %d > %d\n", thread->fd_number,
              thread->fd_size);
    return -1;
  }
  for (int i = 0; i < thread->fd_number; i++) {
    fd_t* fd = thread->fds[i];
    if (fd && kstrcmp(name, fd->name) == 0) {
      return i;
    }
  }
  return -1;
}

int thread_add_fd(thread_t* thread, fd_t* fd) {
  if (thread->fd_number > thread->fd_size) {
    log_error("thread add fd limit\n");
    return -1;
  }
  for (int i = 0; i < thread->fd_number; i++) {
    fd_t* find_fd = thread->fds[i];
    if (find_fd == NULL) {
      thread->fds[i] = fd;
      return i;
    }
  }
  thread->fds[thread->fd_number] = fd;
  return thread->fd_number++;
}

fd_t* thread_find_fd_id(thread_t* thread, u32 fd) {
  if (thread->fd_number > thread->fd_size) {
    log_error("thread find number limit %d\n", fd);
    return NULL;
  }
  if (fd > thread->fd_number) {
    log_error("thread find fd limit %d > %d\n", fd, thread->fd_number);
    return NULL;
  }
  return thread->fds[fd];
}

fd_t* thread_set_fd(thread_t* thread, u32 fd, fd_t* nfd) {
  if (thread->fd_number > thread->fd_size) {
    log_error("thread set number limit %d\n", fd);
    return NULL;
  }
  if (fd > thread->fd_number) {
    log_error("thread set fd limit %d\n", fd);
    return NULL;
  }
  return thread->fds[fd] = nfd;
}

void thread_dump_fd(thread_t* thread) {
  for (int i = 0; i < thread->fd_number; i++) {
    fd_t* fd = thread->fds[i];
    kprintf("tid:%d fd:%d id:%d ptr:%x name:%s\n", thread->id, i, fd->id, fd,
            fd->name);
  }
}

void thread_dump(thread_t* thread, u32 flags) {
  if (thread == NULL) return;
  if (thread->dump_count >= THREAD_DUMP_STOP_COUNT) {
    log_error("thread dump count >= %d, will not dump again\n",
              THREAD_DUMP_STOP_COUNT);
    return;
  }
  vmemory_area_t* vm = vmemory_area_find_flag(thread->vm->vma, MEMORY_STACK);
  thread->dump_count++;
  kprintf("id       %d\n", thread->id);
  if (thread->name != NULL) {
    kprintf("name   %s\n", thread->name);
  }
  kprintf("priority %d\n", thread->priority);
  kprintf("counter  %d\n", thread->counter);
  kprintf("state    %d\n", thread->state);
  kprintf("ksp      %08x  [%8x - %8x]\n", thread->ctx->ksp,
          thread->ctx->ksp_start, thread->ctx->ksp_end);
  kprintf("usp      %08x  [%8x - %8x]\n", thread->ctx->usp, vm->alloc_addr,
          vm->alloc_addr + vm->alloc_size);
  kprintf("pid      %d\n", thread->pid);
  kprintf("fd_num   %d\n", thread->fd_number);
  kprintf("code     %d\n", thread->code);

  if (flags & DUMP_CONTEXT == DUMP_CONTEXT) {
    kprintf("--context--\n");
    context_dump(thread->ctx);
  }
  if (flags & DUMP_STACK == DUMP_STACK) {
    kprintf("--kstack--\n");
    // thread_dump_stack(thread->ctx->ksp_start, thread->ctx->ksp_size);
    int dump_size = 0x100;
    thread_dump_stack(thread->ctx->ksp_end - dump_size, dump_size);
    kprintf("--ustack--\n");
    thread_dump_stack(vm->vend - dump_size, dump_size);
  }
  kprintf("\n");
}

void thread_dump_stack(u32* stack, u32 size) {
  int line_width = 24;
  int offset = (u32)stack;
  char* buffer = stack;
  for (int i = 0; i < size; i++) {
    if (i % line_width == 0) {
      if (i == 0) {
        kprintf(" %07x   ", offset);
      } else {
        kprintf("\n %07x   ", offset);
      }
    }
    kprintf("%02x ", 0xff & buffer[i]);
    offset++;
  }
  kprintf("\n");
}

void thread_dumps() {
  char* state_str[7] = {"create",   "running", "runnable", "stopped",
                        "waitting", "sleep",   "unkown"};
  char* str = "unkown";
  kprintf(
      "id   pid  name           state     cpu  count  "
      "  vm   pm   nstack  file  sleep  level\n");
  for (int i = 0; i < MAX_CPU; i++) {
    for (thread_t* p = schedulable_head_thread[i]; p != NULL; p = p->next) {
      if (p->state <= THREAD_SLEEP) {
        str = state_str[p->state];
      }
      kprintf("%-4d ", p->id);
      kprintf("%-4d ", p->pid);

      if (p->name != NULL) {
        kprintf("%-14s ", p->name);
      } else {
        kprintf("   ");
      }
      kprintf("%-8s %4d %6d %4dk %4dk %4dk %4d %6d     %1d\n", str, p->cpu_id,
              p->counter,
              p->vm->vma != NULL ? p->vm->vma->alloc_size / 1024 : 0,
              p->mem / 1024, p->ctx->usp_size / 1024, p->fd_number,
              p->sleep_counter, p->level);
    }
  }
}

void thread_run_all() {
  thread_t* v = schedulable_head_thread[cpu_get_id()];
  for (; v; v = v->next) {
    thread_run(v);
  }
}

int thread_count() {
  int count = 0;
  for (int i = 0; i < MAX_CPU; i++) {
    for (thread_t* p = schedulable_head_thread[i]; p != NULL; p = p->next) {
      count++;
    }
  }
  return count;
}