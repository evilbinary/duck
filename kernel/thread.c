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
  u32 size = THREAD_STACK_SIZE;
  thread_t* thread = thread_create_ex(entry, size, data, level, 1, NULL);
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
                                u32 level, u32 page) {
  thread_t* t = thread_create_ex(entry, size, data, level, page, NULL);
  if (t == NULL) return t;
  char* kname = kmalloc(kstrlen(name));
  kstrcpy(kname, name);
  t->name = kname;
  return t;
}

thread_t* thread_create_ex(void* entry, u32 size, void* data, u32 level,
                           u32 page, vmemory_area_t* vmm) {
  if (size <= 0) {
    log_error("thread create ex size is 0\n");
    return NULL;
  }

#ifdef NO_THREAD_STACK0
  u8* stack0 = NULL;
#else
  u8* stack0 = kmalloc(size);
#endif
  u8* stack3 = kmalloc_alignment(size, PAGE_SIZE);

  thread_t* thread = kmalloc(sizeof(thread_t));
  thread->lock = 0;
  thread->data = data;
  thread->fd_size = 40;
  thread->fd_number = 0;
  thread->fds = kmalloc(sizeof(fd_t) * thread->fd_size);
  thread->stack_size = size;

  // vfs
  thread->vfs = kmalloc(sizeof(vfs_t));
  // file description
  thread_fill_fd(thread);

  // vm
  u8* vstack3 = stack3;
  u32 koffset = 0;
  if (vmm == NULL) {
    vstack3 = STACK_ADDR;
    if (level == KERNEL_MODE) {
      koffset += KERNEL_OFFSET;
    }
    thread->vmm = vmemory_create_default(vstack3, size, koffset);
  }
  // check stack0 and stack3
  if (stack0 == NULL || stack3 == NULL || vstack3 == NULL) {
    log_error("create thread failt for stack is null\n");
    return NULL;
  }
  thread_init_self(thread, entry, stack0, vstack3 + koffset, size, level);

  // init page
  thread_t* current = thread_current();
  if (current != NULL) {
    thread->context.kernel_page_dir = current->context.kernel_page_dir;
    thread->context.page_dir = current->context.page_dir;

    if (page == PAGE_CLONE) {
      thread->context.page_dir =
          page_alloc_clone(current->context.page_dir, level);
    } else if (page == PAGE_ALLOC) {
      thread->context.page_dir = page_alloc_clone(NULL, level);
    }

  } else {
    if (page == PAGE_ALLOC) {
      thread->context.page_dir = page_alloc_clone(NULL, level);
    }
  }
  if (vmm == NULL) {
    for (int i = 0; i < (size / PAGE_SIZE + 1); i++) {
      map_page_on(thread->context.page_dir, vstack3, stack3,
                  PAGE_P | PAGE_USU | PAGE_RWW);
      vstack3 += PAGE_SIZE;
      stack3 += PAGE_SIZE;
    }
  }

  return thread;
}

thread_t* thread_copy(thread_t* thread, u32 flags) {
  if (thread == NULL) {
    return NULL;
  }
  thread_t* copy = kmalloc(sizeof(thread_t));

  kmemmove(copy, thread, sizeof(thread_t));
  copy->id = thread_ids++;
  copy->next = NULL;
  copy->state = THREAD_CREATE;
  copy->priority = thread->priority;
  copy->counter = thread->counter;
  copy->vmm = thread->vmm;
  copy->data = thread->data;
  copy->pid = thread->id;
  copy->name = thread->name;
  copy->counter = 0;
  copy->fault_count = 0;
  copy->sleep_counter = 0;

  int size = thread->stack_size;

  // copy stack0
  copy->stack0 = kmalloc_alignment(size, PAGE_SIZE);
  // copy->stack0=kvirtual_to_physic(copy->stack0,size);
  copy->stack0_top = copy->stack0 + size;
  if (copy->stack0 == NULL) {
    log_error("stack0 is null\n");
    return -1;
  }
  kmemmove(copy->stack0, thread->stack0, size);

  if (flags & STACK_CLONE) {
    // copy stack3
    copy->stack3 = kmalloc_alignment(size, PAGE_SIZE);
    copy->stack3_top = copy->stack3 + size;
    kmemmove(copy->stack3, thread->stack3, size);
  } else {
    copy->stack3 = thread->stack3;
    copy->stack3_top = thread->stack3_top;
  }

  if (flags & FS_CLONE) {
    // copy files
    copy->fd_size = thread->fd_size;
    copy->fd_number = thread->fd_number;
    copy->fds = kmalloc(sizeof(fd_t) * thread->fd_size);
    kmemmove(copy->fds, thread->fds, sizeof(fd_t) * thread->fd_size);
  }

  if (flags & VM_CLONE) {
    copy->vmm = vmemory_clone(thread->vmm, 0);
  } else if (flags & VM_CLONE_ALL) {
    copy->vmm = vmemory_clone(thread->vmm, 1);
  } else if (flags & VM_SAME) {
    copy->vmm = thread->vmm;
  } else {
    copy->vmm = vmemory_create_default(copy->stack3, size, 0);
  }

  if (flags & PAGE_CLONE) {
    // copy page
    copy->context.page_dir =
        page_alloc_clone(thread->context.page_dir, thread->level);

  } else if (flags & PAGE_SAME) {
    copy->context.page_dir = thread->context.page_dir;
  } else if (flags & PAGE_ALLOC) {
    // todo
  } else if (flags & PAGE_COPY_ON_WRITE) {
    // todo
  }

  // copy context
  log_debug("copy_stack0: %x copy_stack3: %x \n", copy->stack0, copy->stack3);
  context_clone(&copy->context, &thread->context, copy->stack0_top,
                copy->stack3_top, thread->stack0_top, thread->stack3_top);

  interrupt_context_t* context = copy->context.esp0;
  context_ret(context) = 0;

  return copy;
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

void thread_init_self(thread_t* thread, void* entry, u32* stack0, u32* stack3,
                      u32 size, u32 level) {
  u8* stack0_top = stack0;
  stack0_top += size;
  u8* stack3_top = stack3;
  stack3_top += size;
  thread->stack0 = stack0;
  thread->stack3 = stack3;
  thread->id = thread_ids++;
  thread->next = NULL;
  thread->priority = 1;
  thread->counter = 0;
  thread->sleep_counter = 0;
  thread->state = THREAD_CREATE;
  thread->stack0_top = stack0_top;
  thread->stack3_top = stack3_top;
  thread->level = level;
  thread->entry = entry;
  thread->cpu_id = cpu_get_id();
  context_init(&thread->context, (u32*)entry, stack0_top, stack3_top, level,
               thread->cpu_id);
}

void thread_set_entry(thread_t* thread, void* entry) {
  if (thread == NULL) return;
  context_set_entry(&thread->context, entry);
}

void thread_set_arg(thread_t* thread, void* arg) {
  if (thread == NULL) return;
  interrupt_context_t* context = thread->context.esp0;
  context_ret(context) = arg;
}

void thread_set_params(thread_t* thread, void* args, int size) {
  if (thread == NULL) return;
  // todo copy
}

void thread_reset_stack3(thread_t* thread, u32* stack3) {
  // todo free stack3
  //  void* phy = virtual_to_physic(thread->context.page_dir, stack3);
  //  if(phy!=NULL){
  //    kfree(phy);
  //  }else{
  //    kfree(thread->stack3);
  //  }
  thread->stack3 = stack3;
  thread->stack3_top = stack3 + thread->stack_size;
  thread_init_self(thread, thread->entry, thread->stack0, stack3,
                   thread->stack_size, thread->level);
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
  if (thread->stack0 != NULL) {
    // kfree(thread->stack0);
  }
  if (thread->stack3 != NULL) {
    // kfree(thread->stack3);
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
  // free page alloc
  page_free(thread->context.page_dir, thread->level);
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
  kprintf("stack0   %x - %x\n", thread->stack0,thread->stack0_top);
  kprintf("stack3   %x - %x\n", thread->stack3,thread->stack3_top);
  kprintf("pid      %d\n", thread->pid);
  kprintf("fd_num   %d\n", thread->fd_number);
  kprintf("code     %d\n", thread->code);
  kprintf("--context--\n");
  context_dump(&thread->context);
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
              p->stack_size / 1024, p->fd_number);
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