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
#ifdef PERF_MODULE
extern void perf_init_syscall(void** syscall_table);
#endif

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
  /* 【vfork(posix_spawn) 必须给独立 VM】CLONE_VM|CLONE_VFORK 的语义是"共享地址
   * 空间、父进程挂起，子进程马上 exec"。Linux 在 exec 时 unshare mm；本内核的
   * run_elf_thread 直接重建当前线程的地址空间，若与父进程 VM_SAME，exec 会把父
   * 进程的页表/映射一起改掉 ⇒ 父进程恢复后在用户态取指炸飞（实测 system()：
   * ls 能跑出结果，但调用者 test-system 随后 PREF ABORT pc=0x1024）。
   * vfork 子进程不需要共享内存（马上 exec），直接给它一份独立 VM（fork 语义）即可。
   * 只有 pthread 那种"非 VFORK 的 CLONE_VM"才真正需要共享地址空间。 */
  if ((flags & CLONE_VM) != 0 && (flags & CLONE_VFORK) == 0) {
    tflags |= VM_SAME;
  } else {
    tflags |= VM_CLONE_ALL;
  }

  /* 【不要再从 stack 顶读 start_func/start_arg】
   * musl 的 pthread_create 会把 struct start_args 放在栈顶，但 posix_spawn
   * 直接传 stack+sizeof stack（栈顶没有 start_args）。若统一按 pthread 布局去读，
   * posix_spawn 会读到未初始化值当入口 ⇒ 子进程跳飞（system() 卡死）。
   * musl 的 arm clone.s 约定：子进程从 svc 之后继续，r0=0，并用 r5/r6 调
   * func(arg)。r5/r6 在父进程 svc 时的帧里已保存，context_inherit_live 会带过来，
   * 所以这里只需 r0=0 并让子进程沿用被复制的 pc 即可，两种调用都成立。 */

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

  thread_set_ret(copy_thread, 0); /* clone child return value (r0=0) */

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
    log_error("brk: end %x exceeds heap vend %x, returning -1\n",
              end, vm->vend);
    return -1;
  }
  int size = (int)((u32)end - (u32)vm->alloc_addr);
  if (size > 0) {
    if (sys_mmap_child_overlaps(vm->child, (u32)vm->alloc_addr, (u32)size)) {
      log_error("brk: end %x overlaps mmap, keep %x\n", end, vm->alloc_addr);
      return -1;
    }
    /* Allocate physical pages for the expanded heap region so that musl
     * mallocng (and any brk-based allocator) can safely write metadata
     * and payloads without hitting unmapped pages. */
    if (valloc((void*)vm->alloc_addr, (size_t)size) == NULL) {
      log_error("brk: valloc failed for %x size %d\n", vm->alloc_addr, size);
      return -1;
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
    /* 【anon 无条件分配物理页】mallocng 的 meta area 走 mmap(PROT_NONE)+
     * mprotect(RW) 两步；内核 mprotect 未实现(ENOSYS)，mallocng 容忍后直接
     * 写这页。若 prot=0 时缺页靠 page_fault 补，时序上时好时坏 ⇒ 堆元数据
     * 偶发损坏（gnuboy/testmalloc 实测 a_crash）。这里无条件 valloc+清零，
     * 让两步流程闭环；PROT_NONE 的"不可访问"语义在单任务场景可接受。 */
    if (valloc(start_addr, length) == NULL) {
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
  /* YiYiYa·修复：支持一次 munmap 覆盖【多个/部分】area ✗→✓
   * 原实现要求 [addr, addr+size) 完全落在【单个】area 内；而 mallocng 的 trim
   * 会把多个相邻 mmap 合并成一次 munmap ⇒ 永远 area==NULL ⇒ 静默 return ⇒
   * 物理页/映射【从不释放】⇒ 页池耗尽 ⇒ valloc failed / ya_sbrk oom 卡死 ✗✗。
   * 现在：与 [addr,end) 相交的 area 逐个 vfree；完全包含的整块摘除，
   * 部分重叠的裁剪端点（另一端保持；中间被挖空时重复 vfree 是无害的 ✓）。 */
  {
    vaddr_t a0 = (vaddr_t)addr;
    vaddr_t a1 = a0 + size;
    int freed = 0;
    vmemory_area_t* prev = NULL;
    vmemory_area_t* p = vm->child;
    while (p != NULL) {
      vmemory_area_t* next = p->next;
      if (a0 < p->vend && p->vaddr < a1) {
        vaddr_t s0 = a0 > p->vaddr ? a0 : p->vaddr;
        vaddr_t s1 = a1 < p->vend ? a1 : p->vend;
        vfree((void*)s0, (vaddr_t)(s1 - s0));
        freed++;
        if (a0 <= p->vaddr && a1 >= p->vend) {
          /* 完全包含 ⇒ 摘除整块 ✓ */
          if (prev == NULL) {
            vm->child = next;
          } else {
            prev->next = next;
          }
          kfree(p);
          p = next; /* prev 不变 ✓ */
          continue;
        }
        /* 部分重叠 ⇒ 裁剪端点（保留剩余）✓ */
        if (p->vaddr >= a0) {
          p->vaddr = s1;
        } else {
          p->vend = s0;
        }
      }
      prev = p;
      p = next;
    }
    if (freed == 0) {
      static u32 dbg_unmap_miss = 0;
      if (dbg_unmap_miss < 20) {
        dbg_unmap_miss++;
        log_error("munmap: no area intersects %x len=%x\n", (u32)a0,
                  (u32)size);
      }
    }
    return 0;
  }
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
  /* 硬件线程指针（ARM 上是 TPIDRURO）交给架构层设置：ARMv5 没有该寄存器，
   * 由 cpu_set_tls() 的弱实现（arch.c）兜底为空。详见 arch/cpu.h。 */
  cpu_set_tls(set);
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

/* 【重要·应用其实走的是这一路】musl 的 __clock_nanosleep 在本平台
 * （SYS_clock_nanosleep(230) != SYS_clock_nanosleep_time64(407)）且 tv_sec 为
 * 小值时，会**跳过 407 分支**、回落到 __syscall_cp(SYS_nanosleep) —— 也就是
 * 系统调用 162，进到这里。排查睡眠相关问题时务必同时覆盖这一路
 * （曾因只改 sys_clock_nanosleep 而得出"应用没发请求"的错误结论）。 */
int sys_nanosleep(struct timespec* req, struct timespec* rem) {
  if (req == NULL) {
    return -1;
  }
  {
    /* YiYiYa·保险丝（治标但立刻见效）：用户态有路径在传 ~1.6-1.9 秒的延时
     * （来源未定 ✗），一睡就把整个 UI 冻住、手柄像没反应 ✗。这里把【任何】
     * >100ms 的单次睡眠夹到 100ms：语义上只是"更频繁地醒来重新请求"（轮询 ✓），
     * 不会漏事件 ✓，也不会再出现 1.9 秒的大冻 ✓。 */
    u32 tick = (u32)(SECOND_TO_TICK(req->tv_sec) +
                     NANOSECOND_TO_TICK(req->tv_nsec));
    if (tick > 100) {
      tick = 100;
    }
    schedule_sleep(tick);
  }
  return 0;
}

int sys_clock_nanosleep(int clock, int flag, struct timespec* req,
                        struct timespec* rem) {
  if (req == NULL) {
    return -1;
  }
  {
    /* YiYiYa·保险丝（治标但立刻见效）：用户态有路径在传 ~1.6-1.9 秒的延时
     * （来源未定 ✗），一睡就把整个 UI 冻住、手柄像没反应 ✗。这里把【任何】
     * >100ms 的单次睡眠夹到 100ms：语义上只是"更频繁地醒来重新请求"（轮询 ✓），
     * 不会漏事件 ✓，也不会再出现 1.9 秒的大冻 ✓。 */
    u32 tick = (u32)(SECOND_TO_TICK(req->tv_sec) +
                     NANOSECOND_TO_TICK(req->tv_nsec));
    if (tick > 100) {
      tick = 100;
    }
    schedule_sleep(tick);
  }
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
  /* 【关键】user_tp 还没设置时绝不能把内核 tinfo 地址返回给用户态！
   * 编译器/musl 的 TLS 读取（__aeabi_read_tp / __get_tp → SYS_THREAD_SELF）
   * 会把这个返回值当 TLS 基址；返回内核地址 ⇒ 用户态去读写内核内存 ⇒
   * mallocng 的 TLS/errno 状态被写坏 ⇒ 堆元数据损坏、a_crash（gnuboy 实测）。
   * Linux 在 TPIDRURO 未设置时也返回 0，musl 在 __set_thread_area 之前不会
   * 真正使用 TLS，所以返回 0 是安全且正确的。 */
  return 0;
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

/* 【time64 布局写入辅助】内核 struct timespec 是 { int64 tv_sec; long tv_nsec; }
 * —— tv_nsec 只有 4 字节（偏移 8，其后 4 字节是填充）。但 musl 的 clock_gettime
 * 按 time64 布局读取 tv_nsec 的【8 字节】，高 4 字节若没写就是调用者栈上的旧数据
 * ⇒ 应用拿到的时间值是乱数。反汇编已证实旧写法只发 4 字节存储：
 *   `strd r2,[r1]`（tv_sec 8 字节）+ `str r2,[r3,#8]`（tv_nsec 只有 4 字节）。
 * 这里显式按 16 字节 time64 布局写满。
 * 注意：不能直接改 struct timespec 本身——struct stat 内嵌了它
 * （st_atim/st_mtim/st_ctim），改布局会破坏 stat 的应用可见 ABI。 */
struct timespec64_layout {
  long long tv_sec;
  long long tv_nsec;
};

static inline void sys_time64_write(struct timespec* ts, long long sec,
                                    long long nsec) {
  struct timespec64_layout* t = (struct timespec64_layout*)ts;
  t->tv_sec = sec;
  t->tv_nsec = nsec;
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
    /* 【必须按 time64 布局写满 16 字节】见上面 sys_time64_write 的说明：
     * 旧写法只写 4 字节 tv_nsec ⇒ 高 4 字节是栈上垃圾 ⇒ 应用侧的
     * clock_gettime 返回值变成乱数。实测后果：infones 的帧节流用它判断
     * "是否到了下一帧时刻"，比较永远不成立 ⇒ 从不调用 nanosleep ⇒
     * 帧率怎么都降不下来（固定 81fps）。 */
    sys_time64_write(ts, (long long)(ticks / SCHEDULE_FREQUENCY),
                     (long long)((ticks % SCHEDULE_FREQUENCY) *
                                 (1000000000u / SCHEDULE_FREQUENCY)));
    return 0;
  }
  if (clockid == CLOCK_REALTIME || clockid == CLOCK_REALTIME_COARSE) {
    time_t seconds;
    sys_time(&seconds);
    u64 ticks = schedule_get_ticks();
    sys_time64_write(ts, (long long)seconds,
                     (long long)((ticks % SCHEDULE_FREQUENCY) *
                                 (1000000000u / SCHEDULE_FREQUENCY)));
    return 0;
  }
  if (clockid == CLOCK_THREAD_CPUTIME_ID ||
      clockid == CLOCK_PROCESS_CPUTIME_ID) {
    /* 同样必须写满 time64 的 16 字节（旧写法只写 4 字节 tv_nsec ⇒ 高 4 字节
     * 是调用者栈上的旧数据）。 */
    sys_time64_write(ts, 0, 0);
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
  /* 【真正结束整个进程】musl 的 exit()/_Exit() 走 SYS_exit_group；原实现
   * 直接 sys_exit（只退当前线程）✗ —— pthread 后台线程（SDL timer 等）
   * 还活着，ps 里进程一直挂着（gnuboy 按 HOME 打印 "HOME -> exit" 后
   * 不退出就是这个）。这里先做与 sys_exit 相同的当前线程收尾，再由
   * thread_exit_group 停掉同进程其它线程并退出当前线程。 */
  thread_t* current = thread_current();
  if (current != NULL && current->clear_child_tid != NULL) {
    int* tidptr = (int*)current->clear_child_tid;
    *tidptr = 0;
  }
  if (current != NULL) {
    thread_exit_group(current, status);
    if (current->tinfo != NULL) {
      ((thread_info_t*)current->tinfo)->detach_state = DT_EXITED;
    }
  }
  /* 与 sys_exit 相同：不能返回 —— 死线程上继续执行会走飞。 */
  schedule_switch();
  for (;;) {
  }
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
  thread_t* current = thread_current();
  if (t == current) {
    /* 自杀：只停自己（调用方随后一般就走 exit 了） */
    thread_stop(t);
    return 0;
  }
  /* 【kill 一个进程 = 结束它的【整个进程】并唤醒其父进程】原实现只有
   * thread_stop(t)（停一个线程）✗：
   *   · pthread 后台线程（SDL timer 等）还活着 ⇒ 进程没真正结束；
   *   · thread_stop 不唤醒父 ⇒ 父在 sys_waitpid 的 THREAD_WAITING 里
   *     睡死（实测：kill gnuboy 后 gnuboy 从 ps 消失，但 sh/ymain 一直
   *     wait，整个 system() 链卡住 ✗）。
   * thread_exit_group：停掉 t 同进程（共享 upage）的所有线程；
   * thread_exit(t)：把 t 标退出并【唤醒其父】，父醒来后在圆环里查到
   * 僵尸即返回（sys_waitpid 的实现就是靠重查 + 唤醒 ✓）。 */
  thread_exit_group(t, 0);
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

/* ===================== futex =====================
 * 【为什么必须按"地址"登记等待者】
 * 原实现里 FUTEX_WAKE 唤醒的是 thread_current()（**调用者自己** ✗），真正的等待者
 * 谁也等不到唤醒，只能靠 sleep 超时兜底；而 FUTEX_WAIT 也只是盲目睡 400 tick。
 *
 * 这条路上站着 SDL 的音频：SDL_mutexP/V → pthread_mutex_lock → musl → futex；
 * SDL_OpenAudio 还会 SDL_CreateThread() 起音频线程。miyoo 的 /dev/dsp 是空壳
 * （codec_init 空、write 直接丢弃、DMA 模块为 dummy）⇒ 音频线程长时间不释放
 * mixer 互斥 ⇒ 主线程在 mutex 上等 ⇒ 现象正是"应用完全卡死、CPU 却空闲、连 60 帧
 * 统计都打不出来"；而 v3s 有真音频驱动、音频线程持续进展 ⇒ 互斥很快释放 ⇒ 不卡。
 *
 * 现在：等待者按地址登记；FUTEX_WAKE 唤醒**登记在该地址上的等待者**；同时保留
 * 400 tick 兜底睡眠（万一没人唤醒也能自己醒来重试，不会比以前更糟）。
 * ================================================ */
#define FUTEX_WAITER_MAX 8
static struct {
  u32* addr;
  thread_t* t;
} futex_waiters[FUTEX_WAITER_MAX];
static u32 futex_dbg;

static void futex_waiter_add(u32* addr, thread_t* t) {
  int i;
  if (t == NULL) {
    return;
  }
  for (i = 0; i < FUTEX_WAITER_MAX; i++) {
    if (futex_waiters[i].t == t) {
      futex_waiters[i].addr = addr;
      return;
    }
  }
  for (i = 0; i < FUTEX_WAITER_MAX; i++) {
    if (futex_waiters[i].t == NULL) {
      futex_waiters[i].t = t;
      futex_waiters[i].addr = addr;
      return;
    }
  }
  /* 表满：退化为旧的"纯超时"行为，功能不受影响 */
}

static u32 futex_waiter_wake(u32* addr) {
  u32 n = 0;
  int i;
  for (i = 0; i < FUTEX_WAITER_MAX; i++) {
    thread_t* w = futex_waiters[i].t;
    if (w == NULL || futex_waiters[i].addr != addr) {
      continue;
    }
    /* 只在"还在等"时唤醒；已经自己超时醒来的直接清槽 */
    if (w->state == THREAD_SLEEP || w->state == THREAD_WAITING) {
      thread_wake(w); /* 同时清零 sleep_counter ⇒ 兜底睡眠也会立刻结束 */
      n++;
    }
    futex_waiters[i].t = NULL;
    futex_waiters[i].addr = NULL;
  }
  return n;
}

int sys_futex(uint32_t* uaddr, int futex_op, uint32_t val,
              const struct timespec* timeout, /* or: uint32_t val2 */
              uint32_t* uaddr2, uint32_t val3) {
  thread_t* current = thread_current();

  /* 【诊断·可删】只打前 16 次：确认应用真的走到 futex、以及 op 类型
   * （0=WAIT / 1=WAKE）。若 gnuboy 卡住时这里完全没有输出 ⇒ 它与 futex 无关。 */
  if (futex_dbg < 16u) {
    futex_dbg++;
    log_info("futex op=%x addr=%x val=%x tid=%d\n", (u32)futex_op & 0xff,
             (u32)(uintptr_t)uaddr, val, current->id);
  }

  if ((futex_op & FUTEX_WAKE) == FUTEX_WAIT) {
    if (uaddr == NULL || *uaddr != val) {
      return EAGAIN;
    }
    /* 登记 + 有界等待：正常由另一线程的 FUTEX_WAKE 唤醒，没人唤醒也会超时醒来 */
    futex_waiter_add(uaddr, current);
    thread_sleep(current, 400);
    return 0;
  }

  if ((futex_op & FUTEX_WAKE) == FUTEX_WAKE) {
    /* 唤醒登记在该地址上的等待者（不是调用者自己 ✗） */
    return (int)futex_waiter_wake(uaddr);
  }

  log_debug("sys futext not impl %d\n", futex_op);
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

  /* 用户态传入的 attr 语义映射（1=设备，2=系统/内核，3/其它=用户）。
   * 旧实现用三个已废弃的属性名，现统一改为用途名 PAGE_DEV / PAGE_KERNEL /
   * PAGE_USER。 */
  if (attr == 1) {
    attr = PAGE_DEV;
  } else if (attr == 2) {
    attr = PAGE_KERNEL;
  } else {
    attr = PAGE_USER;
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
  thread_t* current = thread_current();
  if (current == NULL) {
    return -1;
  }
#ifndef WNOHANG
#define WNOHANG 1
#endif
  for (;;) {
    thread_t* child = thread_find_zombie_child((int)current->id, (int)pid);
    if (child != NULL) {
      /* 【诊断·可删】确认 waitpid 命中的僵尸与其退出码
       * （定位 spawn 链上 system() 返回 -1 的根因） */
      log_info("waitpid tid=%d pid=%d -> child=%d code=%d state=%d\n",
               current->id, (int)pid, (int)child->id, (int)child->code,
               child->state);
      pid_t ret = (pid_t)child->id;
      if (wstatus != NULL) {
        *wstatus = (int)child->code;
      }
      /* Detach from parent so the same zombie is not returned forever
       * (shell reap_zombies would spin and freeze input). */
      child->pid = (u32)-1;
      return ret;
    }
    if (!thread_child_exists((int)current->id, (int)pid)) {
      /* 【诊断·可删】waitpid 走 ECHILD 分支（父/子 id 不匹配时会到这里） */
      log_info("waitpid tid=%d pid=%d ECHILD\n", current->id, (int)pid);
      return -1; /* ECHILD */
    }
    if (options & WNOHANG) {
      return 0;
    }
    /* 阻塞：置 WAITING 后直接切出去（schedule_switch 交换中断帧，
     * 子进程得以运行）；被 thread_exit 唤醒后继续本循环重试。 */
    current->state = THREAD_WAITING;
    schedule_switch();
  }
}

long sys_wait4(pid_t pid, int* wstatus, int options, struct rusage* rusage) {
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
  context_ret(ic) = -ENOSYS;
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
    context_ret(ic) = -ENOSYS;
    log_warn("syscall %d not found handler\n", context_fn(ic));
  }
}

void* sys_fn_get(int no) {
  if (no < 0 || no >= SYSCALL_NUMBER) {
    return NULL;
  }
  return syscall_table[no];
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
#ifdef ARM
  /* pipe2 (ARM nr=359)：musl 的 system()/spawn 路径会调用。直接复用 pipe，
   * 忽略 flags（内核 pipe 无 O_CLOEXEC/O_NONBLOCK 语义）。 */
  syscall_table[359] = &sys_pipe;
#endif
  syscall_table[SYS_GETPID] = &sys_getpid;
  syscall_table[SYS_GETPPID] = &sys_getppid;
  syscall_table[SYS_DUP] = &sys_dup;
  syscall_table[SYS_DUP2] = &sys_dup2;
  syscall_table[SYS_READDIR] = &sys_readdir;
  syscall_table[SYS_NEWSELECT] = &sys_select;
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
  /* 【musl 真正用的号】time64 构建的 musl 里，nanosleep()/usleep() 发的是
   * clock_nanosleep_time64(407)，失败回退 clock_nanosleep_time32(265)；
   * 原先只登记了旧的 230 ⇒ 这两个号取不到处理函数、返回 ENOSYS ⇒
   * 应用侧 sleep 全部变成空操作（实测：infones 的帧节流日志打印正常，
   * 帧率却仍固定 81fps）。处理函数可直接复用：本内核 struct timespec
   * 的 tv_sec 为 int64、tv_nsec 为 long，偏移(0/8)与 time64 布局一致。
   * 用 #ifdef 保护，便于其他未定义这两个号的架构段继续编译。 */
#ifdef SYS_CLOCK_NANOSLEEP_TIME32
  syscall_table[SYS_CLOCK_NANOSLEEP_TIME32] = &sys_clock_nanosleep;
#endif
#ifdef SYS_CLOCK_NANOSLEEP_TIME64
  syscall_table[SYS_CLOCK_NANOSLEEP_TIME64] = &sys_clock_nanosleep;
#endif

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
#ifdef PERF_MODULE
  perf_init_syscall((void**)syscall_table);
#endif
}