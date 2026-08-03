/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "thread.h"

#include "fd.h"
#include "loader.h"
#include "schedule.h"
#include "syscall.h"

thread_t* current_threads[MAX_CPU] = {0};
thread_t* schedulable_head_thread[MAX_CPU] = {0};
thread_t* schedulable_tail_thread[MAX_CPU] = {0};

thread_t* recycle_head_thread = NULL;
thread_t* recycle_tail_thread = NULL;
u32 recycle_head_thread_count = 0;

u32 thread_ids = 0;
lock_t thread_lock;
#if MP_ENABLE
static volatile int thread_create_lock = 0;
#endif

#define log_debug
// #define DEBUG 1

void thread_init() {  }

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
  return thread_create_level(entry, data, LEVEL_USER);
}

thread_t* thread_create_ex_name(char* name, void* entry, u32 size, void* data,
                                u32 level, u32 flags) {
  thread_t* t = thread_create_ex(entry, KERNEL_THREAD_STACK_SIZE, size, data,
                                 level, flags);
  if (t == NULL) return t;
  char* kname = kmalloc(kstrlen(name) + 1, KERNEL_TYPE);
  kstrcpy(kname, name);
  t->name = kname;
  return t;
}

void thread_init_default(thread_t* thread, u32 level, u32* entry, void* data) {
  thread->id = thread_ids++;
  thread->lock = 0;
  thread->next = NULL;
  thread->rt_wait_next = NULL;
  thread->priority = 1;
  thread->priority_base = 1;
  thread->counter = 0;
  thread->sleep_counter = 0;
  thread->state = THREAD_CREATE;
  thread->level = level;
  thread->exec = NULL;
  thread->cpu_id = cpu_get_id();
  thread->fd_size = 40;
  thread->fd_number = 0;
  thread->data = data;
  thread->mem = 0;
  thread->ticks = 0;
  thread->clear_child_tid = NULL;
  thread->user_tp = NULL;
}

thread_t* thread_create_ex(void* entry, u32 kstack_size, u32 ustack_size,
                           void* data, u32 level, u32 flags) {
  if (ustack_size <= 0) {
    log_error("thread create ex  user stack size is 0\n");
    return NULL;
  }
#if MP_ENABLE
  while (__sync_lock_test_and_set(&thread_create_lock, 1)) {}
#endif

  thread_t* thread = kmalloc(sizeof(thread_t), KERNEL_TYPE);
  thread_init_default(thread, level, entry, data);

  thread->fds = kmalloc(sizeof(fd_t*) * thread->fd_size, KERNEL_TYPE);
  if (thread->fds != NULL) {
    kmemset(thread->fds, 0, sizeof(fd_t*) * thread->fd_size);
  }

  // context init
  context_t* ctx = kmalloc(sizeof(context_t), KERNEL_TYPE);
  thread->ctx = ctx;
  ctx->tid = thread->id;

  void* ksp = kmalloc(kstack_size, KERNEL_TYPE);
  void* usp = kmalloc_alignment(ustack_size, PAGE_SIZE, KERNEL_TYPE);
  ctx->ksp_start = (u64)ksp;
  ctx->ksp_end = (u64)ksp + kstack_size;
  ctx->ksp_size = kstack_size;
  ctx->usp = (u64)usp + ustack_size;
  ctx->usp_size = ustack_size;

  u32 mode = USER_MODE;
  if (level == LEVEL_KERNEL || level == LEVEL_KERNEL_SHARE) {
    mode = KERNEL_MODE;
  }

#ifdef VM_ENABLE
  // vm init
  vmemory_t* vm = kmalloc(sizeof(vmemory_t), KERNEL_TYPE);
  thread->vm = vm;
  vm->tid = thread->id;
  vmemory_init(vm, level, (vaddr_t)usp, ustack_size, flags);

  vmemory_area_t* vm_stack = vmemory_area_find_flag(vm->vma, MEMORY_STACK);
  context_init(ctx, ctx->ksp_end, vm_stack->vend, (u64)entry, mode, thread->cpu_id);
#else
  context_init(ctx, ctx->ksp_end, ctx->usp, (u64)entry, mode, thread->cpu_id);
#endif

  // vfs
  thread->vfs = kmalloc(sizeof(vfs_t), KERNEL_TYPE);
  if (thread->vfs != NULL) {
    kmemset(thread->vfs, 0, sizeof(vfs_t));
    thread->vfs->root = vfs_find(NULL, "/");
    thread->vfs->pwd = thread->vfs->root;
    thread->vfs->users = 1;
  }
  // file description
  thread_fill_fd(thread);

  // check thread data
  int ret = thread_check(thread);
#if MP_ENABLE
  __sync_lock_release(&thread_create_lock);
#endif
  return thread;
}

thread_t* thread_copy(thread_t* thread, u32 flags) {
  if (thread == NULL) {
    return NULL;
  }
  log_debug("thread copy start\n");

  thread_t* copy = kmalloc(sizeof(thread_t), KERNEL_TYPE);
  kmemset(copy, 0, sizeof(thread_t));
  kmemmove(copy, thread, sizeof(thread_t));
  copy->tinfo = thread->tinfo;
  copy->user_tp = thread->user_tp;

  log_debug("thread init default\n");

  thread_init_default(copy, thread->level, thread->ctx->eip, thread->data);
  copy->data = thread->data;
  copy->pid = thread->id;
  // copy->name = kmalloc(kstrlen(thread->name), KERNEL_TYPE);
  // kstrcpy(copy->name, thread->name);
  copy->name = thread->name;
  copy->exec = NULL;
  /* 调度按 counter 最低优先；继承父进程 counter 会让新进程长期饿死。
   * 新线程从 0 起跑，跑一会儿自然与其它线程汇合。 */
  copy->counter = 0;
  copy->fault_count = 0;
  copy->sleep_counter = 0;
  copy->dump_count = 0;
  copy->ticks = 0;

  // context init
  context_t* ctx = kmalloc(sizeof(context_t), KERNEL_TYPE);
  kmemset(ctx, 0, sizeof(context_t));

  copy->ctx = ctx;
  ctx->tid = copy->id;

  u64 kstack_size = thread->ctx->ksp_size;
  u64 ustack_size = thread->ctx->usp_size;

  u64 ksp = (u64)kmalloc(kstack_size, KERNEL_TYPE);
  ctx->ksp_start = ksp;
  ctx->ksp_end = ksp + kstack_size;
  ctx->ksp_size = kstack_size;
  ctx->usp = 0;
  ctx->usp_size = ustack_size;

  context_clone(copy->ctx, thread->ctx);
  context_inherit_live(copy->ctx, thread->ctx->ic);

  if (thread->vfs != NULL) {
    copy->vfs = thread->vfs;
    if (copy->vfs->root == NULL) {
      copy->vfs->root = vfs_find(NULL, "/");
    }
    if (copy->vfs->pwd == NULL) {
      copy->vfs->pwd = copy->vfs->root;
    }
    copy->vfs->users++;
  } else {
    copy->vfs = kmalloc(sizeof(vfs_t), KERNEL_TYPE);
    if (copy->vfs != NULL) {
      kmemset(copy->vfs, 0, sizeof(vfs_t));
      copy->vfs->root = vfs_find(NULL, "/");
      copy->vfs->pwd = copy->vfs->root;
      copy->vfs->users = 1;
    }
  }

#ifdef VM_ENABLE
  // vm init
  copy->vm = kmalloc(sizeof(vmemory_t), KERNEL_TYPE);
  copy->vm->tid = copy->id;

  // init vm include stack heap exec
  vmemory_clone(copy->vm, thread->vm, flags);
  if (copy->vm->upage == NULL) {
    log_error("thread copy: vm upage null after clone\n");
    return NULL;
  }
#endif

  // Always give the child its own fd table; optionally share fd_t objects.
  copy->fd_size = thread->fd_size;
  copy->fd_number = 0;
  copy->fds = kmalloc(sizeof(fd_t*) * copy->fd_size, KERNEL_TYPE);
  if (copy->fds == NULL) {
    log_error("thread copy fd alloc failed\n");
    return NULL;
  }
  kmemset(copy->fds, 0, sizeof(fd_t*) * copy->fd_size);
  if (flags & FS_CLONE) {
    copy->fd_number = thread->fd_number;
    for (int i = 0; i < (int)thread->fd_number; i++) {
      copy->fds[i] = thread->fds[i];
      if (copy->fds[i] != NULL) {
        copy->fds[i]->use_count++;
      }
    }
  } else {
    thread_fill_fd(copy);
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

  if (thread->ctx->usp_size <= 0) {
    log_error("create thread %d faild for ustack size is 0\n", thread->id);
    return -1;
  }
  if (thread->ctx->usp <= 0) {
    log_error("create thread %d faild for ustack is 0\n", thread->id);
    return -1;
  }
#ifdef VM_ENABLE
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
    log_error("create thread %d faild for ustack %lx range [%lx - %lx] error\n",
              thread->id, thread->ctx->usp, vm_stack->vaddr, vm_stack->vend);
    return -1;
  }

  void* phy = page_v2p((u64*)thread->vm->upage, (void*)vm_stack->alloc_addr);
  if (phy == NULL) {
    log_error("thread map have error\n");
    return -1;
  }
#ifdef DEBUG
  vmemory_dump(thread->vm);
  thread_dump(thread, 0);
#endif
  log_debug("tid %d kpage %x upage %x\n", thread->id, thread->vm->kpage,
            thread->vm->upage);
#endif
  log_debug("tid %d ksp %x usp %x\n", thread->id, thread->ctx->ksp,
            thread->ctx->usp);

  return 0;
}

void thread_fill_fd(thread_t* thread) {
  if (thread == NULL || thread->fds == NULL) {
    return;
  }
  fd_ensure_stdio();
  thread->fds[STDIN] = fd_find(STDIN);
  thread->fds[STDOUT] = fd_find(STDOUT);
  thread->fds[STDERR] = fd_find(STDERR);
  thread->fd_number = 0;
  for (int i = STDIN; i <= STDERR; i++) {
    if (thread->fds[i] != NULL) {
      thread->fds[i]->use_count++;
      thread->fd_number = i + 1;
    }
  }
}

void thread_ensure_stdio(thread_t* thread) {
  if (thread == NULL || thread->fds == NULL) {
    return;
  }
  fd_ensure_stdio();
  for (int i = STDIN; i <= STDERR; i++) {
    if (thread->fds[i] == NULL) {
      fd_t* stdfd = fd_find(i);
      if (stdfd != NULL) {
        thread->fds[i] = stdfd;
        stdfd->use_count++;
      }
    }
    if (thread->fds[i] != NULL && i + 1 > (int)thread->fd_number) {
      thread->fd_number = i + 1;
    }
  }
}

void thread_exec_reset_fds(thread_t* thread) {
  if (thread == NULL || thread->fds == NULL) {
    return;
  }
  for (u32 i = 0; i < thread->fd_number; i++) {
    if (i <= STDERR) {
      continue;
    }
    if (thread->fds[i] != NULL) {
      fd_t* f = thread->fds[i];
      thread->fds[i] = NULL;
      fd_close(f);
    }
  }
  thread_ensure_stdio(thread);
}

void thread_sleep(thread_t* thread, u32 count) {
  thread->state = THREAD_SLEEP;
  if(count>0){
    thread->sleep_counter += count;
    thread->counter += count;
  }
}

void thread_wait(thread_t* thread) {
#ifdef DEBUG_THREAD
  log_debug("thread %d wait==============> %d\n", current_threads[cpu_id]->id,
            thread->id);
#endif
  thread->state = THREAD_WAITING;
  schedule_switch();
}

void thread_wake(thread_t* thread) {
#ifdef DEBUG_THREAD
  log_debug("thread %d wake==============> %d\n", current_threads[cpu_id]->id,
            thread->id);
#endif
  thread->state = THREAD_RUNNING;
  thread->sleep_counter = 0;
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

void thread_reset_user_context(thread_t* thread, void* entry, void* stack_top) {
  if (thread == NULL || thread->ctx == NULL || thread->ctx->ksp_end == 0) {
    log_error("thread reset user context invalid thread\n");
    return;
  }
  context_t* ctx = thread->ctx;
#if defined(ARM64)
  context_init(ctx, ctx->ksp_end, (u64)stack_top, (u64)entry, LEVEL_USER,
               thread->cpu_id);
#else
  context_init(ctx, (u32*)ctx->ksp_end, (u32*)stack_top, (u32*)entry, LEVEL_USER,
               thread->cpu_id);
#endif
  interrupt_context_t* ic = context_exec_live(ctx);
  if (ic == NULL) {
    return;
  }
  /* musl _start: first arg = stack pointer (argc/argv/auxv on stack) */
#if defined(ARM64)
  context_arg0(ic) = (u64)stack_top;
  context_arg1(ic) = 0;
  context_arg2(ic) = 0;
  context_arg3(ic) = 0;
#else
  context_arg0(ic) = (u32)stack_top;
  context_arg1(ic) = 0;
  context_arg2(ic) = 0;
  context_arg3(ic) = 0;
#endif
}

void thread_set_ret(thread_t* thread, u32 ret) {
  if (thread == NULL) return;
  interrupt_context_t* ic = thread->ctx->ic;
  if (ic == NULL) {
    ic = thread->ctx->ksp;
  }
  if (ic == NULL) {
    log_error("context is null cannot set ret\n");
    return;
  }
  context_ret(ic) = ret;
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
  u32 parent_id = thread->pid;
  thread_stop(thread);
  thread_t* parent = thread_find_id((int)parent_id);
  if (parent != NULL && parent->state == THREAD_WAITING) {
    thread_wake(parent);
  }
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
    thread->state = THREAD_RUNNING;
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
  if (t == NULL) {
    return NULL;
  }
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
      fd->use_count++;
      return i;
    }
  }
  thread->fds[thread->fd_number] = fd;
  fd->use_count++;
  return thread->fd_number++;
}

fd_t* thread_find_fd_id(thread_t* thread, u32 fd) {
  if (thread == NULL || thread->fds == NULL) {
    return NULL;
  }
  if (thread->fd_number > thread->fd_size) {
    log_error("thread find number limit %d\n", fd);
    return NULL;
  }
  if (fd >= thread->fd_size) {
    log_error("thread find fd limit %d >= size %d\n", fd, thread->fd_size);
    return NULL;
  }
  if (thread->fds[fd] == NULL) {
    if (fd <= STDERR) {
      thread_ensure_stdio(thread);
    }
    if (thread->fds[fd] == NULL) {
      return NULL;
    }
  }
  if (fd + 1 > thread->fd_number) {
    thread->fd_number = fd + 1;
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

int thread_map(thread_t* thread, vaddr_t virt_addr, vaddr_t phy_addr, vaddr_t size) {
  log_debug("thread map %lx %lx %ld\n", virt_addr, phy_addr, size);
  vmemory_map((u64*)thread->vm->upage, virt_addr, phy_addr, size);
  return 0;
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
  kprintf("usp      %08x\n", thread->ctx->usp);
  kprintf("pid      %d\n", thread->pid);
  kprintf("fd_num   %d\n", thread->fd_number);
  kprintf("code     %d\n", thread->code);

  if ((flags & DUMP_CONTEXT) == DUMP_CONTEXT) {
    kprintf("--context--\n");
    context_dump(thread->ctx);
  }
  if ((flags & DUMP_STACK) == DUMP_STACK) {
    kprintf("--kstack--\n");
    // thread_dump_stack(thread->ctx->ksp_start, thread->ctx->ksp_size);
    int dump_size = 0x10;
    thread_dump_stack(thread->ctx->ksp_end - dump_size, dump_size);
#ifdef VM_ENABLE
    if (thread->vm != NULL && thread->vm->vma != NULL) {
      vmemory_area_t* vm = vmemory_area_find_flag(thread->vm->vma, MEMORY_STACK);
      if (vm != NULL) {
        kprintf("--ustack--\n");
        thread_dump_stack(vm->vend - dump_size, dump_size);
        kprintf("kpage    %08x\n", thread->vm->kpage);
        kprintf("upage    %08x\n", thread->vm->upage);
        kprintf("usp_rng  [%8x - %8x]\n", vm->alloc_addr,
                vm->alloc_addr + vm->alloc_size);
      }
    }
#endif
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
  u32 idle_ticks[MAX_CPU];
  u32 cpu_total[MAX_CPU];
  int i;

  for (i = 0; i < MAX_CPU; i++) {
    idle_ticks[i] = 0;
    cpu_total[i] = schedule_get_ticks_cpu(i);
  }
  for (i = 0; i < MAX_CPU; i++) {
    for (thread_t* p = schedulable_head_thread[i]; p != NULL; p = p->next) {
      if (p->name != NULL && kstrcmp((char*)p->name, "idle") == 0) {
        int c = (int)p->cpu_id;
        if (c >= 0 && c < MAX_CPU) {
          idle_ticks[c] += p->ticks;
        }
      }
    }
  }

  kprintf("cpu");
  for (i = 0; i < MAX_CPU; i++) {
    u32 busy = 0;
    if (cpu_total[i] > 0) {
      u32 idle = idle_ticks[i];
      if (idle > cpu_total[i]) {
        idle = cpu_total[i];
      }
      busy = 100 - (idle * 100) / cpu_total[i];
    }
    kprintf(" %d:%d%%", i, busy);
  }
  kprintf("\n");

  kprintf(
      "id   pid  name                 state     cpu  cpu%%  count  "
      "  vm   pm   nstack  file  sleep  level  faults ticks\n");
  for (i = 0; i < MAX_CPU; i++) {
    for (thread_t* p = schedulable_head_thread[i]; p != NULL; p = p->next) {
      u32 pct = 0;
      u32 tot;
      u32 busy_tot;
      int is_idle =
          (p->name != NULL && kstrcmp((char*)p->name, "idle") == 0);
      if (p->state <= THREAD_SLEEP) {
        str = state_str[p->state];
      }
      tot = (p->cpu_id < MAX_CPU) ? cpu_total[p->cpu_id] : 0;
      /* idle: usage 0%（空闲已反映在顶部 cpu N:X%）；其它按占 busy 时间比例 */
      if (!is_idle && tot > 0 && p->cpu_id < MAX_CPU) {
        busy_tot = tot - idle_ticks[p->cpu_id];
        if (busy_tot > 0) {
          pct = (p->ticks * 100) / busy_tot;
          if (pct > 100) {
            pct = 100;
          }
        }
      }
      kprintf("%-4d ", p->id);
      kprintf("%-4d ", p->pid);

      if (p->name != NULL) {
        kprintf("%-20s ", p->name);
      } else {
        kprintf("%-20s ", "");
      }
      kprintf("%-8s %4d %4d%% %6d %4dk %4dk %4dk %6d %6d %6d  %4d %6d\n", str,
              p->cpu_id, pct, p->counter,
              p->vm != NULL && p->vm->vma != NULL
                  ? p->vm->vma->alloc_size / 1024
                  : 0,
              p->mem / 1024, p->ctx->usp_size / 1024, p->fd_number,
              p->sleep_counter, p->level, p->faults, p->ticks);
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

thread_t* thread_find_id(int id) {
  for (int i = 0; i < MAX_CPU; i++) {
    for (thread_t* p = schedulable_head_thread[i]; p != NULL; p = p->next) {
      if (p->id == id) {
        return p;
      }
    }
  }
  return NULL;
}