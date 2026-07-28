/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "sysfn.h"

#include "kernel/devfn.h"
#include "kernel/elf.h"
#include "kernel/event.h"
#include "kernel/fd.h"
#include "kernel/kernel.h"
#include "kernel/loader.h"
#include "kernel/thread.h"
#include "kernel/vfs.h"
#include "../loader/loader.h"

#define log_debug 

static void* syscall_table[SYSCALL_NUMBER];
extern vnode_t* root_node;
extern long xwin_syscall_handler(u32 num, long a1, long a2, long a3, long a4,
                                 long a5, long a6);

static int sys_mmap_pages_mapped(thread_t* current, void* addr, size_t length) {
  if (current == NULL || current->vm == NULL || addr == NULL || length == 0) {
    return 0;
  }
  u32 start = (u32)addr & ~(PAGE_SIZE - 1);
  u32 end = ((u32)addr + length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  for (u32 va = start; va < end; va += PAGE_SIZE) {
    if (page_v2p(current->vm->upage, (void*)va) != NULL) {
      return 1;
    }
  }
  return 0;
}

static int sys_mmap_child_overlaps(vmemory_area_t* child, u32 addr, size_t length) {
  u32 end = addr + length;
  for (vmemory_area_t* p = child; p != NULL; p = p->next) {
    if (p->flags == MEMORY_FREE) {
      continue;
    }
    if (addr < p->vend && end > p->vaddr) {
      return 1;
    }
  }
  return 0;
}

static void* sys_mmap_pick_anon_addr(thread_t* current, vmemory_area_t* vm,
                                     size_t length) {
  if (current == NULL || vm == NULL || length == 0) {
    return NULL;
  }
  if ((uintptr_t)vm->vaddr + length > vm->vend) {
    return NULL;
  }
  u32 try = (vm->vend - length) & ~(PAGE_SIZE - 1);
  while (try >= vm->vaddr) {
    if (!sys_mmap_pages_mapped(current, (void*)try, length) &&
        !sys_mmap_child_overlaps(vm->child, try, length)) {
      return (void*)try;
    }
    if (try <= vm->vaddr) {
      break;
    }
    try -= PAGE_SIZE;
  }
  return NULL;
}

static int sys_mmap_install_area(vmemory_area_t* vm, void* start_addr,
                                 size_t length) {
  vmemory_area_t* reuse = NULL;
  if (vm->child != NULL) {
    for (vmemory_area_t* p = vm->child; p != NULL; p = p->next) {
      if (p->flags == MEMORY_FREE && p->size >= length &&
          p->vaddr == (vaddr_t)start_addr) {
        reuse = p;
        break;
      }
    }
  }
  if (reuse != NULL) {
    reuse->flags = MEMORY_MMAP;
    reuse->size = length;
    reuse->vend = reuse->vaddr + length;
    return 0;
  }
  if (vm->child == NULL) {
    vm->child = vmemory_area_create(start_addr, length, MEMORY_MMAP);
    return vm->child != NULL ? 0 : -1;
  }
  vmemory_area_t* last_area = vmemory_area_find_last(vm->child);
  if (last_area == NULL) {
    return -1;
  }
  vmemory_area_t* new_area = vmemory_area_create(start_addr, length, MEMORY_MMAP);
  if (new_area == NULL) {
    return -1;
  }
  last_area->next = new_area;
  return 0;
}

// #define log_debug  // 取消注释以禁用调试日志

int sys_print(char* s) {
  thread_t* current = thread_current();
  // kprintf("sys print %d %s\n", current->id, s);
  kprintf("%s", s);
  return 0;
}

void sys_test() {
  thread_t* current = thread_current();
  kprintf("sys test %d\n", current->id);

  kprintf("-------dump thread %d-------------\n", current->id);
  thread_dump(current, DUMP_DEFAULT | DUMP_CONTEXT);
}

void sys_dumps() {
  thread_t* current = thread_current();
  thread_dumps();
}

int sys_print_at(char* s, u32 x, u32 y) {
  kprintf("%s", s);
  return 0;
}


size_t sys_yeild() { thread_yield(); }

void sys_exit(int status) {
  thread_t* current = thread_current();
  if (current != NULL && current->id > 1) {
    log_debug("sys_exit tid=%d status=%d\n", current->id, status);
  }
  if (current != NULL && current->clear_child_tid != NULL) {
    int* tidptr = (int*)current->clear_child_tid;
    *tidptr = 0;
  }
  thread_exit(current, status);
  // thread_dumps();
  // log_debug("sys exit tid %d %s status %d\n", current->id, current->name,
  //           status);
  // while (current == thread_current()) {
  //   cpu_sti();
  // }
  // cpu_cli();
  sys_thread_self();
  if (current->tinfo != NULL) {
    ((thread_info_t*)current->tinfo)->detach_state = DT_EXITED;
  }
  // sys_exit must not return to the caller. Once the current thread is marked
  // stopped, immediately switch away; otherwise execution continues on a dead
  // thread and eventually faults in unrelated code paths.
  schedule_switch();
  for (;;) {
  }
}

void* sys_vmap(void* addr, size_t size) {
  thread_t* current = thread_current();
  vmemory_area_t* area = vmemory_area_create(
      current->vm->vma->vaddr + current->vm->vma->size, size, MEMORY_HEAP);
  vmemory_area_add(current->vm->vma, area);
  return area->vaddr;
}

void sys_vumap(void* ptr, size_t size) {
  thread_t* current = thread_current();
  vmemory_area_t* area = vmemory_area_find(current->vm->vma, ptr, size);
  if (area == NULL) return;
  vmemory_area_free(area);
}

void* sys_valloc(void* addr, size_t size) { return valloc(addr, size); }

void* sys_vheap() {
  thread_t* current = thread_current();
  return current->vm->vma->alloc_addr;
}

void sys_vfree(void* addr) {
  // todo
  vfree(addr, PAGE_SIZE);
}

static void exec_params_free(exec_params_t* exec) {
  if (exec == NULL) {
    return;
  }
  if (exec->argv != NULL) {
    for (int i = 0; i < exec->argc; i++) {
      if (exec->argv[i] != NULL) {
        kfree(exec->argv[i]);
      }
    }
    kfree(exec->argv);
  }
  if (exec->envp != NULL) {
    for (int i = 0; i < exec->envc; i++) {
      if (exec->envp[i] != NULL) {
        kfree(exec->envp[i]);
      }
    }
    kfree(exec->envp);
  }
  if (exec->filename != NULL) {
    kfree(exec->filename);
  }
  kfree(exec);
}

static char* exec_dup_string(const char* src) {
  if (src == NULL) {
    return NULL;
  }
  size_t len = kstrlen(src) + 1;
  char* out = kmalloc(len, KERNEL_TYPE);
  if (out == NULL) {
    return NULL;
  }
  kstrcpy(out, src);
  return out;
}

static exec_params_t* exec_params_build(const char* filename, char* const argv[],
                                        char* const envp[]) {
  int argc = 0;
  int envc = 0;
  size_t string_bytes = 0;
  int i = 0;
  while (argv != NULL && argv[i] != NULL) {
    if (argv[i][0] != '\0') {
      argc++;
      string_bytes += kstrlen(argv[i]) + 1;
    }
    i++;
  }
  if (argc == 0) {
    argc = 1;
    string_bytes += kstrlen(filename) + 1;
  }
  if (envp != NULL) {
    for (i = 0; i < 38 && envp[i] != NULL; i++) {
      envc++;
      string_bytes += kstrlen(envp[i]) + 1;
    }
  }

  exec_params_t* exec = kmalloc(sizeof(exec_params_t), KERNEL_TYPE);
  if (exec == NULL) {
    return NULL;
  }
  kmemset(exec, 0, sizeof(exec_params_t));
  exec->filename = exec_dup_string(filename);
  exec->argc = argc;
  exec->envc = envc;
  exec->string_bytes = string_bytes;
  exec->argv = kmalloc(sizeof(char*) * (argc + 1), KERNEL_TYPE);
  exec->envp = kmalloc(sizeof(char*) * (envc + 1), KERNEL_TYPE);
  if (exec->filename == NULL || exec->argv == NULL || exec->envp == NULL) {
    exec_params_free(exec);
    return NULL;
  }
  kmemset(exec->argv, 0, sizeof(char*) * (argc + 1));
  kmemset(exec->envp, 0, sizeof(char*) * (envc + 1));

  int pos = 0;
  for (i = 0; argv != NULL && argv[i] != NULL; i++) {
    if (argv[i][0] == '\0') {
      continue;
    }
    exec->argv[pos] = exec_dup_string(argv[i]);
    if (exec->argv[pos] == NULL) {
      exec_params_free(exec);
      return NULL;
    }
    pos++;
  }
  if (pos == 0) {
    exec->argv[0] = exec_dup_string(filename);
    if (exec->argv[0] == NULL) {
      exec_params_free(exec);
      return NULL;
    }
  }
  for (i = 0; i < envc; i++) {
    exec->envp[i] = exec_dup_string(envp[i]);
    if (exec->envp[i] == NULL) {
      exec_params_free(exec);
      return NULL;
    }
  }
  return exec;
}

u32 sys_exec(char* filename, char* const argv[], char* const envp[]) {
  thread_t* current = thread_current();
  if (current == NULL) {
    log_error("sys exec current is null\n");
    return -1;
  }
  log_debug("sys exec file %s addr %x tid name %s\n", filename, filename,
            current->name);
  // filename = kpage_v2p(filename, 0);
  if (filename == NULL) {
    log_error("sys exec file is null\n");
    return -1;
  }

  if (argv == NULL) {
    log_error("sys exec argv is null %x\n", argv);
    return -1;
  }

  exec_params_t* exec = exec_params_build(filename, argv, envp);
  if (exec == NULL) {
    log_error("sys exec build params failed %s\n", filename);
    return -1;
  }
  log_debug("sys exec %s argc=%d argv0=%s argv1=%s\n", exec->filename,
            exec->argc,
            exec->argv != NULL && exec->argv[0] != NULL ? exec->argv[0] : "-",
            exec->argc > 1 && exec->argv[1] != NULL ? exec->argv[1] : "-");

  int fd = (int)sys_open_kernel(exec->filename, 0);
  if (fd < 0) {
    log_error("sys exec file not found %s\n", exec->filename);
    exec_params_free(exec);
    return -1;
  }
  fd_t* f = thread_find_fd_id(current, fd);
  if (f == NULL) {
    log_error("read not found fd %d tid %d\n", fd, current->id);
    sys_close(fd);
    exec_params_free(exec);
    return -1;
  }
  vnode_t* node = f->data;
  if (node == NULL) {
    log_error("sys exec node is null pwd\n");
    sys_close(fd);
    exec_params_free(exec);
    return -1;
  }
  sys_close(fd);

  if (current->vfs != NULL) {
    if (!vfs_node_is_valid(current->vfs->root)) {
      current->vfs->root = root_node;
    }
    if (!vfs_node_is_valid(current->vfs->pwd)) {
      current->vfs->pwd = current->vfs->root;
    }
  }

  if (current->exec != NULL) {
    exec_params_free(current->exec);
  }
  current->exec = exec;
  current->name = exec->filename;
  thread_fill_fd(current);
#if defined(ARM64) || defined(__aarch64__)
  if (run_elf64_thread((long*)exec) < 0) {
    return -1;
  }
#else
  if (run_elf_thread((long*)exec) < 0) {
    return -1;
  }
#endif
  return 0;
}

int sys_clone(int flags, void* stack, int* parent_tid, void* tls,
              int child_tid) {
  thread_t* current = thread_current();
  if (current == NULL) {
    log_error("current is null\n");
    return -1;
  }

  // 如果 stack 为 NULL，按 fork 方式处理（vfork 会这样调用）
  if (stack == NULL) {
    thread_t* copy_thread = thread_copy(current, THREAD_VFORK);
    thread_set_ret(copy_thread, 0);  // 子进程返回 0

#ifdef LOG_DEBUG
    log_debug("-------dump current thread %d %s-------------\n", current->id);
    thread_dump(current, DUMP_DEFAULT | DUMP_CONTEXT);
    log_debug("-------dump clone thread %d-------------\n", copy_thread->id);
    thread_dump(copy_thread, DUMP_DEFAULT | DUMP_CONTEXT);
#endif

    thread_run(copy_thread);
    return copy_thread->id;  // 父进程返回子进程ID
  }

  /*
   * musl pthread: __clone(start, stack, CLONE_VM|...|CLONE_SETTLS, args, ...)
   * ARM clone.s expects child to resume after svc with r0=0 and call start(args).
   * YiYiYa instead starts at start_args on the new stack (same layout).
   * Must share VM (CLONE_VM) or SDL timer/audio threads see a stale heap copy
   * and PREF ABORT (pc=0) walking timers/mutexes.
   */
  u32 tflags = FS_CLONE;
  if ((flags & CLONE_VM) != 0) {
    tflags |= VM_SAME;
  } else {
    tflags |= VM_CLONE_ALL;
  }

  start_args_t* start_args = stack;
  void* fn = start_args->start_func;
  void* arg = start_args->start_arg;

  thread_t* copy_thread = thread_copy(current, tflags);
  if (copy_thread == NULL) {
    log_error("sys_clone: thread_copy failed\n");
    return -1;
  }

  if (parent_tid != NULL && (flags & CLONE_PARENT_SETTID) != 0) {
    *parent_tid = copy_thread->id;
  }

  /* CLONE_SETTLS: tls is already TP_ADJ(pthread). Do not keep parent's TP. */
  if ((flags & CLONE_SETTLS) != 0 && tls != NULL) {
    copy_thread->user_tp = tls;
#if defined(TLS_ABOVE_TP)
    copy_thread->tinfo = (thread_info_t*)((char*)tls - sizeof(thread_info_t));
#else
    copy_thread->tinfo = tls;
#endif
  } else if (tls != NULL) {
    copy_thread->user_tp = tls;
    copy_thread->tinfo = ((char*)tls) - sizeof(thread_info_t);
  }

  /* Run on the pthread stack (mmap), not the parent's process stack. */
  interrupt_context_t* ic = copy_thread->ctx->ksp;
  if (ic != NULL) {
    ic->sp = (u32)stack;
    copy_thread->ctx->usp = (u32)stack;
  }

  thread_set_ret(copy_thread, 0); /* clone child return value */
  thread_set_arg(copy_thread, arg);
  thread_set_entry(copy_thread, fn);

  (void)child_tid;
  thread_run(copy_thread);
  return copy_thread->id;
}

int sys_vfork() {
  thread_t* current = thread_current();
  if (current == NULL) {
    log_error("current is null\n");
    return -1;
  }
  thread_t* copy_thread = thread_copy(current, THREAD_VFORK);
  thread_set_ret(copy_thread, 0);  // 子进程返回 0

#ifdef LOG_DEBUG
  log_debug("-------dump current thread %d %s-------------\n", current->id);
  thread_dump(current, DUMP_DEFAULT | DUMP_CONTEXT);
  log_debug("-------dump clone thread %d-------------\n", copy_thread->id);
  thread_dump(copy_thread, DUMP_DEFAULT | DUMP_CONTEXT);
#endif

  thread_run(copy_thread);
  return copy_thread->id;  // 父进程返回子进程ID
}

int sys_fork() {
  thread_t* current = thread_current();
  if (current == NULL) {
    log_error("current is null\n");
    return -1;
  }
#ifdef DEBUG
  log_debug("sys fork current kstak size %d\n", current->ctx->ksp_size);
#endif
  // thread_stop(current);
  thread_t* copy_thread = thread_copy(current, THREAD_FORK);
  if (copy_thread == NULL) {
    log_error("sys fork thread copy failed\n");
    return -1;
  }

  thread_set_ret(copy_thread, 0);

  thread_run(copy_thread);
  return copy_thread->id;
}

int sys_pipe(int fds[2]) {
  thread_t* current = thread_current();
  vnode_t* node = pipe_make(PAGE_SIZE);
  fd_t* fd0 = fd_open(node, DEVICE_TYPE_VIRTUAL, "/dev/pipe/pipe0");
  fd_t* fd1 = fd_open(node, DEVICE_TYPE_VIRTUAL, "dev/pipe/pipe1");
  fds[0] = thread_add_fd(current, fd0);
  fds[1] = thread_add_fd(current, fd1);
  return 0;
}

int sys_getpid() {
  thread_t* current = thread_current();
  log_debug("sys get pid %d\n", current->id);
  return current->id;
}

int sys_getppid() {
  thread_t* current = thread_current();
  return current->pid;
}



int sys_brk(u32 end) {
  thread_t* current = thread_current();
  log_debug("brk tid:%d req:%x\n", current->id, end);
  vmemory_area_t* vm = vmemory_area_find_flag(current->vm->vma, MEMORY_HEAP);
  if (vm == NULL) {
    log_error("sys brk not found vm\n");
    return -1;
  }
  if (end == 0) {
    if (vm->alloc_addr == 0) {
      vm->alloc_addr = vm->vaddr + end;
    }
    end = vm->alloc_addr;
    log_debug("brk first ret:%x\n", end);
    return end;
  }
  if ((u32)end > (u32)vm->vend) {
    log_error("brk: end %x exceeds heap vend %x, returning current brk %x\n",
              end, vm->vend, vm->alloc_addr);
    return vm->alloc_addr;
  }
  int size = (int)((u32)end - (u32)vm->alloc_addr);
  if (size > 0) {
    if (sys_mmap_child_overlaps(vm->child, (u32)vm->alloc_addr, (u32)size)) {
      log_error("brk: end %x overlaps mmap, keep %x\n", end, vm->alloc_addr);
      return vm->alloc_addr;
    }
    /* Allocate physical pages for the expanded heap region so that musl
     * mallocng (and any brk-based allocator) can safely write metadata
     * and payloads without hitting unmapped pages. */
    if (valloc((void*)vm->alloc_addr, (size_t)size) == NULL) {
      log_error("brk: valloc failed for %x size %d\n", vm->alloc_addr, size);
      return vm->alloc_addr;
    }
  }
  if (size < 0) {
    log_debug("brk shrink %x by %d\n", end, -size);
    vfree((void*)end, (size_t)(-size));
  }
  int addr = end;
  vm->alloc_size += size;
  vm->alloc_addr = end;
  log_debug("brk old:%x new:%x size_delta:%d total:%d\n",
            addr - size, addr, size, vm->alloc_size);
  return addr;
}

void* sys_mmap2(void* addr, size_t length, int prot, int flags, int fd,
                size_t pgoffset) {
  int ret = 0;
  thread_t* current = thread_current();
  if (current != NULL && current->id > 1) {
    log_debug("sys_mmap2 tid=%d addr=%x len=%d prot=%x flags=%x fd=%d off=%d\n",
              current->id, addr, length, prot, flags, fd, pgoffset);
  }
  vmemory_area_t* vm = vmemory_area_find_flag(current->vm->vma, MEMORY_HEAP);
  if (vm == NULL) {
    log_error("sys mmap2 not found vm\n");
    return MAP_FAILED;
  }
#ifdef LOG_MMAP
  log_debug(
      "sys mmap2 addr=%x length=%d prot = %x,flags = %x, fd = %d, "
      "pgoffset = %d "
      "\n",
      addr, length, prot, flags, fd, pgoffset);
#endif
  if (length <= 0) {
    log_error("map failed length 0\n");
    return MAP_FAILED;
  }

  // 内存大小 对齐 16 page-aligned
  length = ALIGN(length, PAGE_SIZE);

  if (fd > 0) {
    fd_t* f = thread_find_fd_id(current, fd);
    if (f == NULL) {
      log_error("map file not found fd %d tid %d\n", fd, current->id);
      return 0;
    }
    log_error("map file %s %d faild not support\n", f->name, fd);
    return MAP_FAILED;
  }

  if ((flags & MAP_FIXED) == MAP_FIXED) {
    vmemory_area_t* findvm = vmemory_area_find(current->vm->vma, addr, length);
    if (findvm == NULL) {
      log_error("map fix %x faild out of range\n", addr);
      return MAP_FAILED;
    }
    if (prot != 0 && valloc(addr, length) == NULL) {
      log_error("map fix valloc failed addr=%x len=%x\n", addr, length);
      return MAP_FAILED;
    }
    if (sys_mmap_install_area(vm, addr, length) < 0) {
      log_error("map fix install area failed addr=%x\n", addr);
      return MAP_FAILED;
    }
    return addr;
  }

  void* start_addr = NULL;
  if ((flags & MAP_ANON) == MAP_ANON) {
    start_addr = sys_mmap_pick_anon_addr(current, vm, length);
    if (start_addr == NULL) {
      log_error("mmap: no free anon region len=%x\n", length);
      return MAP_FAILED;
    }
    if (prot != 0 && valloc(start_addr, length) == NULL) {
      log_error("mmap anon valloc failed addr=%x len=%x\n", start_addr, length);
      return MAP_FAILED;
    }
    if (sys_mmap_install_area(vm, start_addr, length) < 0) {
      log_error("mmap install area failed addr=%x\n", start_addr);
      return MAP_FAILED;
    }
    log_debug("mmap anon addr %x len %x prot %x\n", start_addr, length, prot);
    return start_addr;
  }

  // Legacy/file-backed path below (still unused for musl TLS).
  start_addr = addr;

  // 先找合适大小的已释放节点复用，避免地址单向增长导致堆溢出
  vmemory_area_t* reuse = NULL;
  if (vm->child != NULL) {
    for (vmemory_area_t* p = vm->child; p != NULL; p = p->next) {
      if (p->flags == MEMORY_FREE && p->size >= length &&
          !sys_mmap_pages_mapped(current, (void*)p->vaddr, length)) {
        reuse = p;
        break;
      }
    }
  }

  if (reuse != NULL) {
    reuse->flags = MEMORY_MMAP;
    reuse->size = length;
    reuse->vend = reuse->vaddr + length;
    start_addr = (void*)reuse->vaddr;
  } else {
    if (vm->child == NULL) {  // 未分配过
      start_addr = vm->vaddr + vm->size / 2;
    } else {
      vmemory_area_t* last_area = vmemory_area_find_last(vm->child);
      start_addr = last_area->vend;
    }

    if ((uintptr_t)start_addr + length > vm->vend) {
      log_error("mmap: out of heap range start=%x len=%x vend=%x\n",
                start_addr, length, vm->vend);
      return MAP_FAILED;
    }

    if (vm->child == NULL) {
      vm->child = vmemory_area_create(start_addr, length, MEMORY_MMAP);
    } else {
      vmemory_area_t* last_area = vmemory_area_find_last(vm->child);
      vmemory_area_t* new_area = vmemory_area_create(start_addr, length, MEMORY_MMAP);
      last_area->next = new_area;
    }
  }

  if ((flags & MAP_SHARED) == MAP_SHARED) {
#ifdef LOG_MMAP
    log_debug("map shared return addr %x\n", start_addr);
#endif
    if (prot != 0 && valloc(start_addr, length) == NULL) {
      log_error("mmap shared valloc failed addr=%x len=%x\n", start_addr, length);
      return MAP_FAILED;
    }
    return start_addr;
  } else if ((flags & MAP_PRIVATE) == MAP_PRIVATE) {
    log_debug("map private return addr %x\n", start_addr);
    if (prot != 0 && valloc(start_addr, length) == NULL) {
      log_error("mmap private valloc failed addr=%x len=%x\n", start_addr, length);
      return MAP_FAILED;
    }
    return start_addr;
  }
  log_error("map failed end\n");
  return MAP_FAILED;
}

void* sys_mremap(void* old_address, size_t old_size, size_t new_size, int flags,
                 ... /* void *new_address */) {
  (void)old_size;
  thread_t* current = thread_current();
  if (current == NULL || current->vm == NULL) {
    return MAP_FAILED;
  }
  vmemory_area_t* vm = vmemory_area_find_flag(current->vm->vma, MEMORY_HEAP);
  if (vm == NULL) {
    log_error("sys mremap not found vm\n");
    return MAP_FAILED;
  }
  if (old_address == NULL || new_size == 0) {
    return MAP_FAILED;
  }
  if ((flags & MREMAP_FIXED) == MREMAP_FIXED) {
    log_error("mremap: MREMAP_FIXED not supported\n");
    return MAP_FAILED;
  }

  new_size = ALIGN(new_size, PAGE_SIZE);

  log_debug("sys mremap old=%x new=%x flags=%x\n", old_address, new_size, flags);

  vmemory_area_t* old_area = vmemory_area_find(vm->child, old_address, 0);
  if (old_area == NULL || old_area->flags == MEMORY_FREE) {
    log_error("mremap: area not found at %x\n", old_address);
    return MAP_FAILED;
  }
  /* mallocng passes the mapping base (g->mem). */
  if ((vaddr_t)old_address != old_area->vaddr) {
    log_error("mremap: %x is not mapping base %x\n", old_address,
              old_area->vaddr);
    return MAP_FAILED;
  }

  size_t map_old = old_area->size;
  if (new_size == map_old) {
    return old_address;
  }

  /* Shrink: drop trailing pages. */
  if (new_size < map_old) {
    vfree((void*)(old_area->vaddr + new_size), map_old - new_size);
    old_area->size = new_size;
    old_area->vend = old_area->vaddr + new_size;
    return old_address;
  }

  size_t grow = new_size - map_old;
  u32 extend_at = (u32)old_area->vend;
  int can_inplace =
      (extend_at + grow <= (u32)vm->vend) &&
      !sys_mmap_child_overlaps(vm->child, extend_at, grow) &&
      !sys_mmap_pages_mapped(current, (void*)(uintptr_t)extend_at, grow);

  if (can_inplace) {
    if (valloc((void*)(uintptr_t)extend_at, grow) == NULL) {
      log_error("mremap: inplace valloc failed at %x len %x\n", extend_at,
                grow);
      return MAP_FAILED;
    }
    old_area->size = new_size;
    old_area->vend = old_area->vaddr + new_size;
    log_debug("mremap inplace %x size %x\n", old_area->vaddr, new_size);
    return old_address;
  }

  if ((flags & MREMAP_MAYMOVE) == 0) {
    log_error("mremap: cannot grow in place\n");
    return MAP_FAILED;
  }

  /*
   * Old code only bumped vend / created a VMA and never mapped pages or
   * copied — realloc(256K→512K) then wrote into unmapped VA (PREF/data abort).
   */
  void* new_addr = sys_mmap_pick_anon_addr(current, vm, new_size);
  if (new_addr == NULL) {
    log_error("mremap: no anon space for %x\n", new_size);
    return MAP_FAILED;
  }
  if (valloc(new_addr, new_size) == NULL) {
    log_error("mremap: valloc new %x len %x failed\n", new_addr, new_size);
    return MAP_FAILED;
  }
  if (sys_mmap_install_area(vm, new_addr, new_size) < 0) {
    vfree(new_addr, new_size);
    log_error("mremap: install area failed\n");
    return MAP_FAILED;
  }

  kmemcpy(new_addr, old_address, map_old);
  if (sys_munmap(old_address, map_old) != 0) {
    log_warn("mremap: munmap old %x failed\n", old_address);
  }

  log_debug("mremap moved %x -> %x size %x\n", old_address, new_addr, new_size);
  return new_addr;
}

int sys_munmap(void* addr, size_t size) {
  log_debug("sys munmap addr: %x size: %d\n", addr, size);
  thread_t* current = thread_current();
  vmemory_area_t* vm = vmemory_area_find_flag(current->vm->vma, MEMORY_HEAP);
  if (vm == NULL) {
    log_error("sys munmap not found vm\n");
    return MAP_FAILED;
  }
  vmemory_area_t* area = vmemory_area_find(vm->child, addr, size);
  if (area == NULL) {
    log_warn("sys munmap not found area\n");
    return 0;
  }
  // 释放物理页
  vfree((void*)area->vaddr, area->size);
  // 从 child 链表摘掉节点并释放
  vmemory_area_t* prev = NULL;
  vmemory_area_t* p = vm->child;
  while (p != NULL) {
    if (p == area) {
      if (prev == NULL) {
        vm->child = p->next;
      } else {
        prev->next = p->next;
      }
      kfree(area);
      break;
    }
    prev = p;
    p = p->next;
  }

  return 0;
}

int sys_mprotect(const void* start, size_t len, int prot) {
  log_debug("sys mprotect not impl\n");
  return -ENOSYS;
}

int sys_rt_sigprocmask(int h, void* set, void* old_set) {
  return 0;
}

int sys_rt_sigaction(int signum, const struct sigaction* restrict act,
                     struct sigaction* restrict oldact) {
  log_debug("sys sigaction not impl\n");
  return 0;
}

unsigned int sys_alarm(unsigned int seconds) {
  log_debug("sys alarm not impl\n");
  return -1;
}


int sys_set_thread_area(void* set) {
  thread_t* current = thread_current();
  if (current == NULL) {
    return -1;
  }
  if (current->id > 1) {
    log_debug("sys_set_thread_area tid=%d tp=%x\n", current->id, set);
  }
  current->user_tp = set;
#if defined(__arm__)
  asm volatile("mcr p15, 0, %0, c13, c0, 3" : : "r"(set) : "memory");
#endif
  return 0;
}


int sys_umask(int mask) {
  thread_t* current = thread_current();
  if (current == NULL) {
    log_error("current is null\n");
    return -1;
  }
  // todo

  return mask;
}


int sys_self(void* t) {
  if (t == NULL) return -2;
  thread_t* current = thread_current();
  if (current == NULL) return -1;
  kmemcpy(t, current, sizeof(thread_t));
  return 1;
}

int sys_clock_nanosleep(int clock, int flag, struct timespec* req,
                        struct timespec* rem) {
  thread_t* current = thread_current();
  // if (current->id > 3) {
  //   kprintf("sys_clock_nanosleep %d %d\n", req->tv_sec, req->tv_nsec);
  // }
  schedule_sleep(SECOND_TO_TICK(req->tv_sec) +
                 NANOSECOND_TO_TICK(req->tv_nsec));
  return 0;
}

int sys_nanosleep(struct timespec* req, struct timespec* rem) {
  schedule_sleep(SECOND_TO_TICK(req->tv_sec) +
                 NANOSECOND_TO_TICK(req->tv_nsec));
  return 0;
}

int sys_mem_info() {
  memory_t* mem = memory_info();
  kprintf("total       %6dk\n", mem->total / 1024);
  kprintf("free        %6dk\n", mem->free / 1024);
  kprintf("kernel used %6dk\n", mem->kernel_used / 1024);
  kprintf("user   used %6dk\n", mem->user_used / 1024);
}

int sys_info(sysinfo_t* info) {
  memory_t* mem = memory_info();
  info->totalram = mem->total;
  info->freeram = mem->free;
  info->procs = thread_count();
  return 0;
}

int sys_thread_self() {
  thread_t* current = thread_current();
  if (current == NULL) {
    return 0;
  }
  if (current->user_tp != NULL) {
    return (int)current->user_tp;
  }
  thread_info_t* tinfo = current->tinfo;

  if (tinfo == NULL) {
    tinfo = kmalloc(sizeof(thread_info_t), KERNEL_TYPE);
    current->tinfo = tinfo;
    tinfo->self = tinfo;
    tinfo->tid = current->id;
    tinfo->errno = 0;
    tinfo->prev = tinfo->next = NULL;
    tinfo->locale = kmalloc(sizeof(locale_t), KERNEL_TYPE);
    tinfo->robust_list.head = &tinfo->robust_list.head;
    tinfo->detach_state = DT_JOINABLE;
    log_debug("locale at %x\n", tinfo->locale);
    log_debug("thread info at %x\n", tinfo);
    log_debug("tsd at %x\n", tinfo->tsd);
  }
  // log_debug("sys thread self at %x\n", tinfo);
  return TP_ADJ(tinfo);
}

int sys_statx(int dirfd, const char* restrict pathname, int flags,
              unsigned int mask, struct statx* restrict statxbuf) {
  log_debug("sys statx not impl dirfd %d pathname %s\n", dirfd, pathname);

  if (statxbuf == NULL) {
    return -1;
  }

  struct stat buf;
  int ret = sys_stat(pathname, &buf);
  if (ret < 0) {
    return -1;
  }

  statxbuf->stx_mode = buf.st_mode;
  statxbuf->stx_gid = buf.st_gid;
  statxbuf->stx_uid = buf.st_uid;
  statxbuf->stx_size = buf.st_size;
  statxbuf->stx_blocks = buf.st_blocks;

  return 0;
}

u32 sys_time(time_t* t) {
  vnode_t* time_node = vfs_find(NULL, "/dev/time");
  rtc_time_t time;
  time_t seconds = 0;
  time.day = 1;
  time.hour = 0;
  time.minute = 0;
  time.month = 1;
  time.second = 0;
  time.year = 1970;

  if (t == NULL) {
    return -1;
  }
  if (time_node == NULL) {
    log_error("cannot found file /dev/time\n");
    kmemcpy(t, &seconds, sizeof(time_t));
    return 0;
  }

  // kprintf("time fd %d\n", time_fd);
  u32 ret = vread(time_node, 0, sizeof(rtc_time_t), &time);

  // u32 time_fd = -1;
  // if (time_fd == -1) {
  //   time_fd = sys_open("/dev/time", 0);
  // }
  // if (time_fd < 0) {
  //   log_error("open time faild\n");
  //   return 0;
  // }
  // int ret = sys_read(time_fd, &time, sizeof(rtc_time_t));
  if (ret < 0) {
    return 0;
  }
  // kprintf("%d-%d-%d %d:%d:%d\n", time.year, time.month, time.day, time.hour,
  //         time.minute, time.second);

  seconds = secs_of_years(time.year - 1) + secs_of_month(time.month - 1, time.year) +
            (time.day - 1) * 86400 + time.hour * 3600 + time.minute * 60 +
            time.second + 0;

  kmemcpy(t, &seconds, sizeof(time_t));

  return ret;
}

int sys_clock_gettime64(clockid_t clockid, struct timespec* ts) {
  if (ts == NULL) {
    return -1;
  }
  if (clockid == CLOCK_MONOTONIC || clockid == CLOCK_MONOTONIC_RAW ||
      clockid == CLOCK_MONOTONIC_COARSE || clockid == CLOCK_BOOTTIME) {
    /* 单调时钟必须用累计 tick，不能 ticks%1000（会回绕导致 GetTicks 倒退、
     * SDL_Delay/sys_sleep 算出超长睡眠 → 黑屏）。 */
    u64 ticks = schedule_get_ticks();
    ts->tv_sec = (time_t)(ticks / SCHEDULE_FREQUENCY);
    ts->tv_nsec =
        (long)((ticks % SCHEDULE_FREQUENCY) * (1000000000u / SCHEDULE_FREQUENCY));
    return 0;
  }
  if (clockid == CLOCK_REALTIME || clockid == CLOCK_REALTIME_COARSE) {
    time_t seconds;
    sys_time(&seconds);
    u64 ticks = schedule_get_ticks();
    ts->tv_sec = seconds;
    ts->tv_nsec =
        (long)((ticks % SCHEDULE_FREQUENCY) * (1000000000u / SCHEDULE_FREQUENCY));
    return 0;
  }
  if (clockid == CLOCK_THREAD_CPUTIME_ID ||
      clockid == CLOCK_PROCESS_CPUTIME_ID) {
    ts->tv_sec = 0;
    ts->tv_nsec = 0;
    return 0;
  }
  log_warn("clock not support %d\n", clockid);
  return -1;
}

int sys_set_tid_adress(void* ptr) {
  thread_t* current = thread_current();
  if (current != NULL && current->id > 1) {
    log_debug("sys_set_tid_address tid=%d ptr=%x\n", current->id, ptr);
  }
  if (current != NULL) {
    current->clear_child_tid = ptr;
  }
  return current->id;
}

void sys_exit_group(int status) {
  sys_exit(status);
}

ssize_t sys_readlink(const char* restrict pathname, char* restrict buf,
                     size_t bufsiz) {
  log_debug("sys exit group not impl %s\n", pathname);
  return bufsiz;
}

int sys_madvice(void* addr, size_t length, int advice) {
  log_debug("sys madvice not impl %x len:%x %x\n", addr, length, advice);
  return 0;
}

int sys_thread_create(char* name, void* entry, void* data) {
  thread_t* t = thread_create_name(name, entry, data);
  thread_run(t);
  return t;
}

int sys_thread_dump() {
  thread_t* current = thread_current();
  log_debug("========sys dump start========\n");
  thread_dump(current, DUMP_DEFAULT | DUMP_CONTEXT | DUMP_STACK);
  log_debug("========sys dump end========\n");

  return current->id;
}

int sys_kill(pid_t pid, int sig) {
  if (pid <= 0) {
    log_error("cannot kill kernel\n");
    return -1;
  }
  thread_t* t = thread_find_id(pid);
  if (t == NULL) {
    return -1;
  }
  thread_stop(t);
  return 0;
}

void* sys_thread_addr(void* vaddr) {
  void* phy_addr = kpage_v2p(vaddr, 0);
  kprintf("===>>%x=>%x\n", vaddr, phy_addr);
  kprintf("phy_addr value===>%x vaddr value:%x %x\n", *((char*)phy_addr),
          *((char*)vaddr));
  // *((char*)vaddr)= *((char*)phy_addr);
  return phy_addr;
}

int sys_futex(uint32_t* uaddr, int futex_op, uint32_t val,
              const struct timespec* timeout, /* or: uint32_t val2 */
              uint32_t* uaddr2, uint32_t val3) {
  thread_t* current = thread_current();
  if ((futex_op & FUTEX_WAKE) == FUTEX_WAIT) {
    if (*uaddr == val) {
      log_debug("wait %d\n", current->id);
      thread_sleep(current, 400);
    } else {
      // thread_sleep(current, 0);
      return EAGAIN;
    }

  } else if ((futex_op & FUTEX_WAKE) == FUTEX_WAKE) {
    if (*uaddr == val) {
      log_debug("wake %d\n", current->id);

      thread_wake(current);
    } else {
      return EAGAIN;
    }
  } else {
    log_debug("sys futext not impl %d\n", futex_op);
  }

  return 0;
}


int sys_gettid() {
  thread_t* current = thread_current();
  return current->id;
}

int sys_thread_map(int tid, u32 virt_addr, u32 phy_addr, u32 size, u32 attr) {
  thread_t* current = NULL;
  current = thread_find_id(tid);
  if (current == NULL) {
    current = thread_current();
  }
  log_debug("sys thread %s map %x %x %d\n", current->name, virt_addr, phy_addr,
            size);

  if (attr == 1) {
    attr = PAGE_DEV;
  } else if (attr == 2) {
    attr = PAGE_SYS;
  } else if (attr == 3) {
    attr = PAGE_USR;
  } else {
    attr = PAGE_USR;
  }

  int ret = 0;
  vmemory_map_type(current->vm->upage, virt_addr, phy_addr, size, attr);

  return ret;
}


int sys_sched_getparam(int pid, sched_param_t* param) {
  log_debug("sys sched getparam not impl\n");

  return 0;
}

int sys_sched_setparam(int pid, sched_param_t* param) {
  log_debug("sys sched setparam not impl\n");

  return 0;
}

int sys_sched_get_priority_max(int policy) {
  log_debug("sys_sched_get_priority_max not impl %d\n", policy);

  return 0;
}

int sys_sched_get_priority_min(int policy) {
  log_debug("sys_sched_get_priority_min not impl\n");

  return 0;
}

int sys_sched_setscheduler(pid_t pid, int policy,
                           const struct sched_param* param) {
  log_debug("sys_sched_setscheduler not impl\n");

  return 0;
}

pid_t sys_waitpid(pid_t pid, int* wstatus, int options) {
  log_debug("sys_waitpid %d %x %d not impl\n", pid, wstatus, options);
  if (wstatus != NULL) {
    *wstatus = 0;
  }
  if (pid > 0) {
    return pid;
  }
  return -1;
}

pid_t sys_wait4(pid_t pid, int* wstatus, int options, struct rusage* rusage) {
  (void)rusage;
  return sys_waitpid(pid, wstatus, options);
}

int sys_fn_faild_handler(int no, interrupt_context_t* ic) {
  int call_id = context_fn(ic);
  if (call_id == 0xf0005) {
    return sys_fn_call(ic, &sys_set_thread_area);
  }
  if (call_id >= 0x5000 && call_id <= 0x5019) {
    context_ret(ic) =
        xwin_syscall_handler((u32)call_id, (long)context_arg0(ic),
                             (long)context_arg1(ic), (long)context_arg2(ic),
                             (long)context_arg3(ic), (long)context_arg4(ic),
                             (long)context_arg5(ic));
    return 0;
  }
  log_debug("sys fn faild %x\n", call_id);
  return -1;
}

void sys_fn_call_handler(int no, interrupt_context_t* ic) {
  thread_t* current = thread_current();
  if (current != NULL && current->ctx != NULL) {
    current->ctx->ic = ic;
  }
  void* fn = syscall_table[context_fn(ic)];
  if (fn != NULL) {
    // kprintf("syscall fn:%d r0:%x r1:%x r2:%x r3:%x fn addr
    // %x\n",ic->r7,ic->r0,ic->r1,ic->r2,ic->r3,fn);
    if (context_fn(ic) == SYS_EXEC) {
      u32 ret = sys_exec((char*)context_arg0(ic),
                         (char* const*)context_arg1(ic),
                         (char* const*)context_arg2(ic));
      if ((int)ret < 0) {
        context_ret(ic) = ret;
      } else {
        thread_t* current = thread_current();
        if (current != NULL && current->ctx != NULL && current->ctx->ksp != NULL) {
          kmemmove(ic, current->ctx->ksp, sizeof(interrupt_context_t));
          log_debug("sys exec return pc=%x sp=%x tid=%d\n", ic->pc,
                    current->ctx->usp, current->id);
        }
      }
    } else {
      sys_fn_call((ic), fn);
    }
    // kprintf(" ret=%x\n",context_ret(ic));
  } else {
    log_warn("syscall %d not found handler\n", context_fn(ic));
  }
}

void sys_fn_init() {
  sys_fn_regist_faild(sys_fn_faild_handler);
  sys_fn_regist_handler(sys_fn_call_handler);

  syscall_table[SYS_READ] = &sys_read;
  syscall_table[SYS_WRITE] = &sys_write;
  syscall_table[SYS_YIELD] = &sys_yeild;
  syscall_table[SYS_PRINT] = &sys_print;
  syscall_table[SYS_PRINT_AT] = &sys_print_at;
  syscall_table[SYS_IOCTL] = &sys_ioctl;
#if defined(ARM64) || defined(__aarch64__) || defined(ARM)
  // openat(dirfd, path, flags, mode) and legacy open(path, flags, mode).
  syscall_table[SYS_OPEN] = &sys_open_dispatch;
  syscall_table[SYS_OPENAT] = &sys_open_dispatch;
#else
  syscall_table[SYS_OPEN] = &sys_open;
#endif
  syscall_table[SYS_CLOSE] = &sys_close;
  syscall_table[SYS_DEV_READ] = &dev_read;
  syscall_table[SYS_DEV_WRITE] = &dev_write;
  syscall_table[SYS_DEV_IOCTL] = &dev_ioctl;
  syscall_table[SYS_EXEC] = &sys_exec;
  syscall_table[SYS_TEST] = &sys_test;
  syscall_table[SYS_EXIT] = &sys_exit;
  syscall_table[SYS_STOP] = &sys_exit;
  syscall_table[SYS_MAP] = &sys_vmap;
  syscall_table[SYS_UMAP] = &sys_vumap;
  syscall_table[SYS_SEEK] = &sys_seek;
  syscall_table[SYS_VALLOC] = &sys_valloc;
  syscall_table[SYS_VFREE] = &sys_vfree;
  syscall_table[SYS_VHEAP] = &sys_vheap;
  syscall_table[SYS_FORK] = &sys_fork;
  syscall_table[SYS_PIPE] = &sys_pipe;
  syscall_table[SYS_GETPID] = &sys_getpid;
  syscall_table[SYS_GETPPID] = &sys_getppid;
  syscall_table[SYS_DUP] = &sys_dup;
  syscall_table[SYS_DUP2] = &sys_dup2;
  syscall_table[SYS_READDIR] = &sys_readdir;
  syscall_table[SYS_BRK] = &sys_brk;

  syscall_table[SYS_READV] = &sys_readv;
  syscall_table[SYS_WRITEV] = &sys_writev;
  syscall_table[SYS_CHDIR] = &sys_chdir;
  syscall_table[SYS_MMAP2] = &sys_mmap2;
  syscall_table[SYS_MPROTECT] = &sys_mprotect;
  syscall_table[SYS_RT_SIGPROCMASK] = &sys_rt_sigprocmask;
  syscall_table[SYS_RT_SIGACTION] = &sys_rt_sigaction;

  syscall_table[SYS_ALARM] = &sys_alarm;
  // aarch64 uses *at syscalls for many path operations
#if defined(ARM64) || defined(__aarch64__)
  syscall_table[SYS_UNLINK] = &sys_unlinkat;
  syscall_table[SYS_RENAME] = &sys_renameat;
#else
  syscall_table[SYS_UNLINK] = &sys_unlink;
  syscall_table[SYS_RENAME] = &sys_rename;
#endif

  syscall_table[SYS_RENAME] = &sys_rename;

  syscall_table[SYS_SET_THREAD_AREA] = &sys_set_thread_area;
  syscall_table[SYS_DUMPS] = &sys_dumps;

  syscall_table[SYS_GETDENTS64] = &sys_getdents64;
  syscall_table[SYS_GETDENTS] = &sys_getdents64;
  syscall_table[SYS_MUNMAP] = &sys_munmap;

  syscall_table[SYS_FCNT64] = &sys_fcntl64;
  syscall_table[SYS_GETCWD] = &sys_getcwd;
  syscall_table[SYS_CHDIR] = &sys_chdir;
  syscall_table[SYS_FCHDIR] = &sys_fchdir;
  syscall_table[SYS_CLONE] = &sys_clone;
#if defined(ARM64)
  // ARM64 uses lseek (62) which returns 64-bit offset directly.
  // _llseek is an ARM32 legacy syscall, map it to lseek for compatibility.
  syscall_table[SYS_LLSEEK] = &sys_seek;
#else
  syscall_table[SYS_LLSEEK] = &sys_llseek;
#endif

  syscall_table[SYS_UMASK] = &sys_umask;

  // aarch64 syscall 79 is newfstatat
#if defined(ARM64) || defined(__aarch64__)
  syscall_table[SYS_STAT] = &sys_stat_dispatch;
#else
  syscall_table[SYS_STAT] = &sys_stat;
#endif
  syscall_table[SYS_FSTAT] = &sys_fstat;
  syscall_table[SYS_SELF] = &sys_self;

  syscall_table[SYS_CLOCK_NANOSLEEP] = &sys_clock_nanosleep;
  syscall_table[SYS_NANOSLEEP] = &sys_nanosleep;

  syscall_table[SYS_MREMAP] = &sys_mremap;
  syscall_table[SYS_STATX] = &sys_statx;
  syscall_table[SYS_CLOCK_GETTIME64] = &sys_clock_gettime64;

  syscall_table[SYS_SYSINFO] = &sys_info;
  syscall_table[SYS_MEMINFO] = &sys_mem_info;
  syscall_table[SYS_THREAD_SELF] = &sys_thread_self;

  syscall_table[SYS_SET_TID_ADDRESS] = &sys_set_tid_adress;

  syscall_table[SYS_EXIT_GROUP] = &sys_exit_group;
  // aarch64 syscall 78 is readlinkat
#if defined(ARM64) || defined(__aarch64__)
  syscall_table[SYS_READLINK] = &sys_readlink_dispatch;
#else
  syscall_table[SYS_READLINK] = &sys_readlink;
#endif

  syscall_table[SYS_MADVISE] = &sys_madvice;

  syscall_table[SYS_THREAD_CREATE] = &sys_thread_create;
  syscall_table[SYS_THREAD_DUMP] = &sys_thread_dump;

  syscall_table[SYS_KILL] = &sys_kill;
  syscall_table[SYS_THREAD_ADDR] = &sys_thread_addr;

  syscall_table[SYS_FUTEX] = &sys_futex;
  // aarch64 syscall 34 is mkdirat
#if defined(ARM64) || defined(__aarch64__)
  syscall_table[SYS_MKDIR] = &sys_mkdirat;
#else
  syscall_table[SYS_MKDIR] = &sys_mkdir;
#endif
  // aarch64 syscall 48 is faccessat
#if defined(ARM64) || defined(__aarch64__)
  syscall_table[SYS_ACESS] = &sys_access_dispatch;
#else
  syscall_table[SYS_ACESS] = &sys_access;
#endif

  syscall_table[SYS_GETTID] = &sys_gettid;
  syscall_table[SYS_THREAD_MAP] = &sys_thread_map;
  syscall_table[SYS_FSTAT64] = &sys_fstat64;
  syscall_table[SYS_STATFS64] = &sys_statfs64;
  syscall_table[SYS_SCHED_GETPARAM] = &sys_sched_getparam;
  syscall_table[SYS_SCHED_SETPARAM] = &sys_sched_setparam;
  syscall_table[SYS_SCHED_SETSCHEDULER] = &sys_sched_setscheduler;
  syscall_table[SYS_SCHED_GET_PRIORITY_MAX] = &sys_sched_get_priority_max;
  syscall_table[SYS_SCHED_GET_PRIORITY_MIN] = &sys_sched_get_priority_min;

  syscall_table[SYS_WAIT4] = &sys_wait4;

  // Initialize network syscalls
  sys_fn_net_init((void**)syscall_table);
}