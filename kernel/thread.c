/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "thread.h"

#include "fd.h"
#include "loader.h"
#include "syscall.h"

thread_t* current_thread[MAX_CPU] = {0};
thread_t* schedulable_head_thread[MAX_CPU] = {0};
thread_t* schedulable_tail_thread[MAX_CPU] = {0};

thread_t* recycle_head_thread = NULL;
thread_t* recycle_tail_thread = NULL;
u32 recycle_head_thread_count = 0;

u32 thread_ids = 0;
lock_t thread_lock;

void thread_init() { lock_init(&thread_lock); }

thread_t* thread_create_level(void* entry, void* data, u32 level) {
  thread_t* thread =
      thread_create_ex(entry, KERNEL_THREAD_STACK_SIZE, THREAD_STACK_SIZE, data,
                       level, THREAD_DEFAULT);
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
  char* kname = kmalloc(kstrlen(name));
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
  thread->context.tid = thread->id;
  thread->context.eip = entry;
  thread->context.level = level;
  thread->fd_size = 40;
  thread->fd_number = 0;
  thread->data = data;
}

thread_t* thread_create_ex(void* entry, u32 kstack_size, u32 ustack_size,
                           void* data, u32 level, u32 flags) {
  if (ustack_size <= 0) {
    log_error("thread create ex  user stack size is 0\n");
    return NULL;
  }
  thread_t* thread = kmalloc(sizeof(thread_t));
  thread_init_default(thread, level, entry, data);

  thread->fds = kmalloc(sizeof(fd_t) * thread->fd_size);
  thread->context.usp_size = ustack_size;
  thread->context.ksp_size = kstack_size;
  // must set null before init
  thread->context.ksp = NULL;
  thread->context.usp = NULL;
  thread->context.ksp_start = NULL;
  thread->context.usp_start = NULL;
  thread->context.ksp_end = NULL;
  thread->context.usp_end = NULL;
  // vfs
  thread->vfs = kmalloc(sizeof(vfs_t));
  // file description
  thread_fill_fd(thread);

  // init vm include stack heap exec
  thread_t* current = thread_current();
  thread_init_vm(thread, current, flags);

  context_init(&thread->context, entry, thread->level, thread->cpu_id);
  return thread;
}

thread_t* thread_copy(thread_t* thread, u32 flags) {
  if (thread == NULL) {
    return NULL;
  }
  thread_t* copy = kmalloc(sizeof(thread_t));

  kmemmove(copy, thread, sizeof(thread_t));
  thread_init_default(copy, thread->level, thread->context.eip, thread->data);
  copy->vmm = thread->vmm;
  copy->data = thread->data;
  copy->pid = thread->id;
  copy->name = thread->name;
  copy->counter = 0;
  copy->fault_count = 0;
  copy->sleep_counter = 0;

  // must set
  copy->context.ksp = NULL;
  copy->context.usp = NULL;
  copy->context.ksp_start = NULL;
  copy->context.usp_start = NULL;
  copy->context.ksp_end = NULL;
  copy->context.usp_end = NULL;

  thread_init_vm(copy, thread, flags);

  context_clone(&copy->context, &thread->context);
  return copy;
}

int thread_init_vm(thread_t* copy, thread_t* thread, u32 flags) {
  if (copy == NULL) {
    log_error("create thread failt for thread is null\n");
    return -1;
  }

  u32 koffset = 0;
  if (copy->level == KERNEL_MODE) {
    koffset += KERNEL_OFFSET;
  }

  // 文件分配方式
  if (flags & FS_CLONE) {
    // copy file
    copy->fd_size = thread->fd_size;
    copy->fd_number = thread->fd_number;
    copy->fds = kmalloc(sizeof(fd_t) * thread->fd_size);
    kmemmove(copy->fds, thread->fds, sizeof(fd_t) * thread->fd_size);
  }

  //栈分配方式
  // kstack
  if (copy->context.ksp_start == NULL) {
    copy->context.ksp_start = kmalloc(copy->context.ksp_size);
    copy->context.ksp_end = copy->context.ksp_size + copy->context.ksp_start;
    if (thread != NULL && (flags & STACK_CLONE)) {
      kmemmove(copy->context.ksp_start, thread->context.ksp_start,
               thread->context.ksp_size);
    }
  }

  // vmm分配方式
  if (flags & VM_CLONE) {
    copy->vmm = vmemory_clone(thread->vmm, 0);
  } else if (flags & VM_CLONE_ALL) {
    copy->vmm = vmemory_clone(thread->vmm, 1);
    copy->vmm->alloc_addr = thread->vmm->alloc_addr;
    copy->vmm->alloc_size = thread->vmm->alloc_size;
  } else if (flags & VM_SAME) {
    copy->vmm = thread->vmm;
  } else {
    copy->vmm = vmemory_create_default(copy->context.usp_size, koffset);
  }

  // page 分配方式
  if (thread != NULL) {
    copy->context.kpage = thread->context.kpage;

    // 堆栈分配方式
    u32* copy_ustack = NULL;
    if (flags & STACK_ALLOC) {
      copy_ustack = kmalloc_alignment(copy->context.usp_size, PAGE_SIZE);
      copy->context.usp = STACK_ADDR;
      copy->context.usp_start = copy_ustack;
      copy->context.usp_end = copy->context.usp_start + copy->context.usp_size;
    } else if (flags & STACK_CLONE) {
      copy_ustack = kmalloc_alignment(copy->context.usp_size, PAGE_SIZE);
      kmemmove(copy_ustack, thread->context.usp, copy->context.usp_size);
      copy->context.usp = STACK_ADDR;
      copy->context.usp_start = copy_ustack;
      copy->context.usp_end = copy->context.usp_start + copy->context.usp_size;
    } else if (flags & STACK_SAME) {
      copy_ustack = thread->context.usp_start;
    }

    //堆栈分配方式
    u32* copy_heap = NULL;
    int heap_size = MEMORY_HEAP_SIZE;
    if (copy->vmm->alloc_size > 0) {
      heap_size = copy->vmm->alloc_size;
    }
    if (thread == NULL) {  // is kernel mode
      heap_size = PAGE_SIZE;
    }
    if (flags & HEAP_ALLOC) {
      copy_heap = kmalloc_alignment(heap_size, PAGE_SIZE);
    } else if (flags & HEAP_SAME) {
      copy_heap = thread->vmm->vaddr;
    } else if (flags & HEAP_CLONE) {
      copy_heap = kmalloc_alignment(heap_size, PAGE_SIZE);
      kmemmove(copy_heap, thread->vmm->vaddr, heap_size);
    }

    if (flags & PAGE_CLONE) {
      //分配页
      copy->context.upage =
          page_alloc_clone(thread->context.upage, thread->level);
      //映射栈
      void* phy = kvirtual_to_physic(copy_ustack, 0);
      thread_map(copy, STACK_ADDR, phy, copy->context.usp_size);

      //映射堆
      phy = kvirtual_to_physic(copy_heap, 0);
      thread_map(copy, HEAP_ADDR, phy, copy->vmm->alloc_size);

    } else if (flags & PAGE_SAME) {
      copy->context.upage = thread->context.upage;
    } else if (flags & PAGE_ALLOC) {
      copy->context.upage = page_alloc_clone(NULL, copy->level);
    } else if (flags & PAGE_COPY_ON_WRITE) {
      // todo check
      u32 pages = thread->vmm->alloc_size / PAGE_SIZE + 1;
      u32 address = thread->vmm->vaddr;
      for (int i = 0; i < pages; i++) {
        void* phy = kvirtual_to_physic(address, 0);
        map_page_on(copy->context.upage, address, phy,
                    PAGE_P | PAGE_USU | PAGE_RWR);
        address += PAGE_SIZE;
      }
    }
  } else {
    log_debug("kernel start before init\n");
    if (copy->level == KERNEL_MODE) {
      void* ustack = kmalloc_alignment(copy->context.usp_size, PAGE_SIZE);
      copy->context.usp = STACK_ADDR + koffset;
      copy->context.usp_start = ustack;
      copy->context.usp_end = copy->context.usp + copy->context.usp_size;

      void* phy = kvirtual_to_physic(ustack, 0);
      thread_map(copy, STACK_ADDR + koffset, phy, copy->context.usp_size);
      // copy->context.usp_start = kmalloc(copy->context.usp_size);
      // copy->context.usp_end = copy->context.usp_start +
      // copy->context.usp_size;
      copy->context.upage = page_alloc_clone(NULL, copy->level);
    } else if (copy->level == USER_MODE) {
      void* ustack = kmalloc_alignment(copy->context.usp_size, PAGE_SIZE);
      copy->context.usp = STACK_ADDR;
      copy->context.usp_start = ustack;
      copy->context.usp_end = copy->context.usp_start + copy->context.usp_size;

      void* phy = kvirtual_to_physic(ustack, 0);
      thread_map(copy, STACK_ADDR, phy, copy->context.usp_size);
      copy->context.upage = page_alloc_clone(NULL, copy->level);
    }
  }

  // check thread data
  int ret = thread_check(copy);
  return ret;
}

void thread_map(thread_t* thread, u32 virt_addr, u32 phy_addr, u32 size) {
  u32 offset = 0;
  u32 pages = size / PAGE_SIZE + size % PAGE_SIZE == 0 ? 0 : 1;
  for (int i = 0; i < pages; i++) {
    map_page_on(thread->context.upage, virt_addr + offset, phy_addr + offset,
                PAGE_P | PAGE_USU | PAGE_RWW);
    log_debug("thread %d map vaddr: %x - paddr: %x\n", thread->id,
              virt_addr + offset, phy_addr + offset);
    offset += PAGE_SIZE;
  }
}

int thread_check(thread_t* thread) {
  if (thread->context.ksp_start == NULL) {
    log_error("create thread failt for ksp start is null\n");
    return -1;
  }
  if (thread->context.usp_start == NULL) {
    log_error("create thread failt for usp start is null\n");
    return -1;
  }
  if (thread->context.upage == NULL) {
    log_error("create thread failt for page dir is null\n");
    return -1;
  }
  if (thread->context.usp_size <= 0) {
    log_error("create thread failt for ustack is 0\n");
    return -1;
  }
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
  log_debug("thread %d wait==============> %d\n", current_thread[cpu_id]->id,
            thread->id);
#endif
  thread->state = THREAD_WAITING;
  schedule_next();
}

void thread_wake(thread_t* thread) {
#ifdef DEBUG_THREAD
  log_debug("thread %d wake==============> %d\n", current_thread[cpu_id]->id,
            thread->id);
#endif
  thread->state = THREAD_RUNNING;
  thread->sleep_counter = 0;
  schedule_next();
}

void thread_set_entry(thread_t* thread, void* entry) {
  if (thread == NULL) return;
  context_set_entry(&thread->context, entry);
}

void thread_set_arg(thread_t* thread, void* arg) {
  if (thread == NULL) return;
  interrupt_context_t* context = thread->context.ksp;
  context_ret(context) = arg;
}

void thread_set_ret(thread_t* thread, u32 ret) {
  if (thread == NULL) return;
  interrupt_context_t* context = thread->context.ksp;
  context_ret(context) = 0;
}

void thread_set_params(thread_t* thread, void* args, int size) {
  if (thread == NULL) return;
  // todo copy
}

void thread_reset_user_stack(thread_t* thread, u32* ustack) {
  // todo
}

void thread_add(thread_t* thread) {
  lock_acquire(&thread_lock);
  int cpu_id = cpu_get_id();
  if (schedulable_head_thread[cpu_id] == NULL) {
    schedulable_head_thread[cpu_id] = thread;
    schedulable_tail_thread[cpu_id] = thread;
  } else {
    schedulable_tail_thread[cpu_id]->next = thread;
    schedulable_tail_thread[cpu_id] = thread;
  }
  thread->state = THREAD_RUNABLE;
  if (current_thread[cpu_id] == NULL) {
    if (schedulable_head_thread[cpu_id] == NULL) {
      log_error("no thread please create a thread\n");
      cpu_halt();
    }
    current_thread[cpu_id] = schedulable_head_thread[cpu_id];
    // current_context = &current_thread[cpu_id]->context;
  }
  lock_release(&thread_lock);
}

void thread_remove(thread_t* thread) {
  lock_acquire(&thread_lock);
  int cpu_id = cpu_get_id();
  thread_t* prev = schedulable_head_thread[cpu_id];
  thread_t* v = prev->next;
  thread->state = THREAD_STOPPED;
  thread->counter += 1000;

  if (schedulable_head_thread[cpu_id] == thread) {
    schedulable_head_thread[cpu_id] = NULL;
    schedulable_tail_thread[cpu_id] = NULL;
    thread->next = NULL;
    lock_release(&thread_lock);

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
  lock_release(&thread_lock);
}

void thread_destroy(thread_t* thread) {
  if (thread == NULL) return;
  if (thread->context.ksp != NULL) {
    // kfree(thread->context.ksp);
  }
  if (thread->context.usp != NULL) {
    // kfree(thread->context.usp);
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
  // page_free(thread->context.upage, thread->level);
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
  thread_t* t = current_thread[cpu_id];
  // lock_release(&thread_lock);
  return t;
}

void thread_set_current(thread_t* thread) {
  lock_acquire(&thread_lock);
  int cpu_id = cpu_get_id();
  current_thread[cpu_id] = thread;
  lock_release(&thread_lock);
}

context_t* thread_current_context() {
  lock_acquire(&thread_lock);
  int cpu_id = cpu_get_id();
  thread_t* t = current_thread[cpu_id];
  lock_release(&thread_lock);
  return &t->context;
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

void thread_dump(thread_t* thread) {
  if (thread == NULL) return;
  kprintf("id       %d\n", thread->id);
  if (thread->name != NULL) {
    kprintf("name   %s\n", thread->name);
  }
  kprintf("priority %d\n", thread->priority);
  kprintf("counter  %d\n", thread->counter);
  kprintf("state    %d\n", thread->state);
  kprintf("ksp      %x  %x - %x\n", thread->context.ksp,
          thread->context.ksp_start, thread->context.ksp_end);
  kprintf("usp      %x  %x - %x\n", thread->context.usp,
          thread->context.ksp_start, thread->context.usp_end);
  kprintf("pid      %d\n", thread->pid);
  kprintf("fd_num   %d\n", thread->fd_number);
  kprintf("code     %d\n", thread->code);
  kprintf("--context--\n");
  context_dump(&thread->context);
  kprintf("--kstack--\n");
  thread_dump_stack(thread->context.ksp_end - 0x100, 0x100);
  kprintf("--ustack--\n");
  thread_dump_stack(thread->context.usp_end - 0x100, 0x100);
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
      "id   pid  name       state     cpu  count  sleep   "
      "vmm    nstack  file\n");
  for (int i = 0; i < MAX_CPU; i++) {
    for (thread_t* p = schedulable_head_thread[i]; p != NULL; p = p->next) {
      if (p->state <= THREAD_SLEEP) {
        str = state_str[p->state];
      }
      kprintf("%-4d ", p->id);
      kprintf("%-4d ", p->pid);

      if (p->name != NULL) {
        kprintf("%-10s ", p->name);
      } else {
        kprintf("   ");
      }
      kprintf("%-8s %4d %6d %6d %4dk %4dk   %4d\n", str, p->cpu_id, p->counter,
              p->sleep_counter, p->vmm != NULL ? p->vmm->alloc_size / 1024 : 0,
              p->context.usp_size / 1024, p->fd_number);
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