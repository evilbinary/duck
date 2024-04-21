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

static void* syscall_table[SYSCALL_NUMBER] = {0};

// #define log_debug

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

size_t sys_ioctl(u32 fd, u32 cmd, void* args) {
  u32 ret = 0;
  fd_t* f = thread_find_fd_id(thread_current(), fd);
  if (f == NULL) {
    // f = find_fd(fd);
  }
  if (f == NULL) {
    log_error("ioctl not found fd %d\n", fd);
    return 0;
  }
  vnode_t* node = f->data;
  ret = vioctl(node, cmd, args);

  // log_debug("sys ioctl fd %d %s cmd %x ret %x\n", fd, f->name, cmd, ret);
  return ret;
}

u32 sys_open(char* name, int attr, ...) {
  // mm_dump();
  // kprintf("open %s attr %x\n",name,attr&O_CREAT==O_CREAT);
  if (name == NULL) {
    log_error("open name is null\n");
    return -1;
  }
  if (attr > 020200000) {
    log_error("open attr range error %x\n", attr);
    return -1;
  }

  thread_t* current = thread_current();
  if (current == NULL) {
    log_error(" cannot find current thread\n");
    return -1;
  }
  // current pwd
  vnode_t* pwd = NULL;

  if (kstrlen(name) >= 2 && name[0] == '.' && name[1] == '/') {
    kstrcpy(name, &name[2]);
    pwd = current->vfs->pwd;
  } else if (kstrlen(name) == 1 && name[0] == '/') {
    pwd = current->vfs->root;
  } else if (name[0] == '/') {
  } else {
    pwd = current->vfs->pwd;
  }
  char* path_name = kmalloc(256, KERNEL_TYPE);
  vfs_path_append(pwd, name, path_name);

  log_debug("path name %s to %s\n", name, path_name);

  int f = thread_find_fd_name(current, path_name);
  if (f >= 0) {
    fd_t* fd = thread_find_fd_id(current, f);
    fd->offset = 0;
    log_debug("sys open name return : %s fd: %d\n", path_name, f);
    return f;
  }
  vnode_t* file = vfs_open_attr(pwd, name, attr);
  if (file == NULL) {
    log_error("sys open file %s error, attr %x \n", name, attr);
    return -1;
  }

  fd_t* fd = fd_open(file, DEVICE_TYPE_FILE, path_name);
  if (fd == NULL) {
    log_error(" new fd error\n");
    return -1;
  }
  fd->offset = 0;
  f = thread_add_fd(current, fd);
  if (f < 0) {
    log_error("sys open %s error\n", name);
    return -1;
  }
  if (current->id > 0) {
    log_debug(
        "sys open new path name: %s name: %s addr:%x fd:%d fd->id:%d ptr:%x "
        "fd->name:%s\n",
        path_name, name, name, f, fd->id, fd, fd->name);
  }
  return f;
}

int sys_close(u32 fd) {
  thread_t* current = thread_current();
  fd_t* f = thread_find_fd_id(current, fd);
  if (f == NULL) {
    log_error("close not found fd %d tid %d\n", fd, current->id);
    return 0;
  }
  int ret = fd_close(f);
  if (ret == 1) {
    // fd use by other do not close
    thread_set_fd(current, fd, NULL);
  }
  return 0;
}

size_t sys_write(u32 fd, void* buf, size_t nbytes) {
  thread_t* current = thread_current();
  fd_t* f = thread_find_fd_id(current, fd);
  if (f == NULL) {
    log_error("write not found fd %d tid %d\n", fd, current->id);
    return 0;
  }
  vnode_t* node = f->data;
  if (node == NULL) {
    log_error("sys write node is null tid %d \n", current->id);
    return -1;
  }
  // kprintf("sys write %d %s fd:%s\n",current->id,buf,f->name);
  u32 ret = vwrite(node, f->offset, nbytes, buf);
  f->offset += nbytes;
  return ret;
}
size_t sys_read(u32 fd, void* buf, size_t nbytes) {
  thread_t* current = thread_current();
  fd_t* f = thread_find_fd_id(current, fd);
  if (f == NULL) {
    log_error("read not found fd %d tid %d\n", fd, current->id);
    return 0;
  }
  vnode_t* node = f->data;
  if (node == NULL) {
    log_error("sys read node is null\n");
    return -1;
  }
  u32 ret = vread(node, f->offset, nbytes, buf);
  if (ret > 0) {
    f->offset += ret;
  }
#ifdef USE_BLOCK
  if (ret == 0) {
    source_t* source = event_source_io_create(node, f, nbytes, buf);
    event_wait(current, source);
  }
#endif
  return ret;
}

size_t sys_seek(u32 fd, size_t offset, int whence) {
  fd_t* f = thread_find_fd_id(thread_current(), fd);
  if (f == NULL) {
    log_error("seek not found fd %d\n", fd);
    return 0;
  }
  // set start offset
  if (whence == 0) {  // seek set
    f->offset = offset;
  } else if (whence == 1) {  // seek current
    f->offset += offset;
  } else if (whence == 2) {  // seek end
    vnode_t* file = f->data;
    if (file != NULL) {
      f->offset = file->length + offset;
    }
  } else {
    log_error("seek whence error %d\n", whence);
    return -1;
  }
  return f->offset;
}

size_t sys_yeild() { thread_yield(); }

void sys_exit(int status) {
  thread_t* current = thread_current();
  thread_exit(current, status);
  // thread_dumps();
  // log_debug("sys exit tid %d %s status %d\n", current->id, current->name,
  //           status);
  // while (current == thread_current()) {
  //   cpu_sti();
  // }
  // cpu_cli();

  if (current->info != NULL) {
    current->info->detach_state = 0;
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

u32 sys_exec(char* filename, char* const argv[], char* const envp[]) {
  thread_t* current = thread_current();
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

  char* name = kmalloc(kstrlen(filename), KERNEL_TYPE);
  kstrcpy(name, filename);
  current->name = name;

  int fd = sys_open(filename, 0);
  if (fd < 0) {
    log_error("sys exec file not found %s\n", name);
    return -1;
  }
  fd_t* f = thread_find_fd_id(current, fd);
  if (f == NULL) {
    log_error("read not found fd %d tid %d\n", fd, current->id);
    return 0;
  }
  thread_set_entry(current, load_thread_entry);
  vnode_t* node = f->data;
  if (node == NULL) {
    log_error("sys exec node is null pwd\n");
    return -1;
  }
  sys_close(fd);
  if (node->parent != NULL) {
    // current->vfs->pwd = node->parent;
  } else {
    current->vfs->pwd = node;
  }

  // init data
  int argc = 0;
  int i = 0;
  while (argv != NULL && argv[i] != NULL) {
    if (argv[i] != NULL && kstrlen(argv[i]) > 0) {
      log_debug("argv[%d]=%s %x\n", argc, argv[argc], &argv[argc]);
      argc++;
    }
    i++;
  }

  long* args = kmalloc(sizeof(long*) * argc + 4 + 38, KERNEL_TYPE);
  args[0] = argc;
  i = 0;
  int pos = 1;
  for (i = 0; i < argc; i++) {
    args[pos] = argv[i];
    pos++;
  }
  log_debug("envp %x argc %d\n", envp, argc);

  long* p = args;
  char** pargv = (void*)(p + 1);
  char** penvp = pargv + argc + 1;

  args[1] = filename;
  args[pos++] = 0;
  if (envp != NULL) {
    for (i = 0; i < 38 && envp[i]; i++) {
      args[pos++] = envp[i];
    }
  }
  args[pos++] = 0;

  current->exec = args;
  thread_set_arg(current, args);
  thread_run(current);

  kmemmove(current->ctx->ic, current->ctx->ksp, sizeof(interrupt_context_t));

  return args;
}

int sys_clone(int flags, void* stack, int* parent_tid, void* tls,
              int child_tid) {
  thread_t* current = thread_current();
  if (current == NULL) {
    log_error("current is null\n");
    return -1;
  }
  start_args_t* start_args = stack;
  void* fn = start_args->start_func;
  void* arg = start_args->start_arg;

  thread_t* find = current;
  if (*parent_tid > 0) {
    find = thread_find_id(*parent_tid);
  }
  if (find == NULL) {
    log_error("find parent tid %d is null\n", *parent_tid);
    find = current;
  }
  thread_t* copy_thread = thread_copy(find, THREAD_FORK);
#ifdef LOG_DEBUG
  kprintf("-------dump current thread %d %s-------------\n", current->id);
  thread_dump(current, DUMP_DEFAULT | DUMP_CONTEXT);
  kprintf("-------dump clone thread %d-------------\n", copy_thread->id);
  thread_dump(copy_thread, DUMP_DEFAULT | DUMP_CONTEXT);
  kprintf("entry ===>%x arg %x\n", fn, arg);
#endif

  thread_set_ret(copy_thread, copy_thread->id);
  thread_set_arg(copy_thread, arg);
  thread_set_entry(copy_thread, fn);

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

#ifdef LOG_DEBUG
  log_debug("-------dump current thread %d %s-------------\n", current->id);
  thread_dump(current, DUMP_DEFAULT | DUMP_CONTEXT);
  log_debug("-------dump clone thread %d-------------\n", copy_thread->id);
  thread_dump(copy_thread, DUMP_DEFAULT | DUMP_CONTEXT);
#endif

  thread_run(copy_thread);
  return current->id;
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
  thread_set_ret(copy_thread, 0);
#ifdef DEBUG
  log_debug("fork copy finished\n");
#endif
#ifdef LOG_DEBUG
  log_debug("-------dump current thread %d %s-------------\n", current->id);
  thread_dump(current, DUMP_DEFAULT | DUMP_CONTEXT);
  log_debug("-------dump clone thread %d-------------\n", copy_thread->id);
  thread_dump(copy_thread, DUMP_DEFAULT | DUMP_CONTEXT);
#endif
  thread_run(copy_thread);
  thread_run(current);
#ifdef DEBUG
  log_debug("fork end\n");
#endif
  return current->id;
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

int sys_dup(int oldfd) {
  thread_t* current = thread_current();
  fd_t* fd = thread_find_fd_id(current, oldfd);
  if (fd == NULL) {
    log_error("dup not found fd %d\n", oldfd);
    return 0;
  }
  int newfd = thread_add_fd(current, fd);
#ifdef DEBUG_SYS_FN
  log_debug("sys dup %d %s\n", newfd, fd->name);
#endif
  return newfd;
}

int sys_dup2(int oldfd, int newfd) {
  thread_t* current = thread_current();
  fd_t* fd = thread_find_fd_id(current, oldfd);
  if (fd == NULL) {
    log_error("dup not found fd %d\n", fd);
    return 0;
  }
  fd_t* nfd = thread_find_fd_id(current, newfd);
  if (nfd == NULL) {
    log_error("dup not found nfd %d\n", nfd);
    return 0;
  }
  fd_close(fd);
  thread_set_fd(current, newfd, fd);
  return newfd;
}

int sys_readdir(int fd, int index, void* dirent) {
  thread_t* current = thread_current();
  fd_t* findfd = thread_find_fd_id(current, fd);
  if (fd == NULL) {
    log_error("readdir not found fd %d\n", fd);
    return 0;
  }
  u32 ret = vreaddir(findfd->data, dirent, &findfd->offset, index);
  return ret;
}

int sys_readv(int fd, iovec_t* vector, int count) {
  int ret = -1;
  int n;
  int i;
  int num = 0;
  int total = 0;
  int pos = 0;
  for (i = 0; i < count; i++) {
    n = sys_read(fd, vector[pos].iov_base, vector[pos].iov_len);
    if (n > 0) {
      num += n;
      ret = num;
      total += vector[pos].iov_len;
    } else if (n <= 0) {
      break;
    }
    if (num >= total) {
      pos++;
    }
  }
  return ret;
}

int sys_writev(int fd, iovec_t* vector, int count) {
  int ret = 0;
  int n;
  int i;
  if (count == 0) {
    return 0;
  }
  for (i = 0; i < count; i++, vector++) {
    if (vector->iov_base == NULL || vector->iov_len <= 0) {
      continue;
    }
    n = sys_write(fd, vector->iov_base, vector->iov_len);
    if (n < 0) {
      return n;
    }
    ret += n;
    if (n != vector->iov_len) break;
  }
  return ret;
}

int sys_chdir(const char* path) {
  int ret = 0;
  thread_t* current = thread_current();
  int fd = sys_open(path, 0);
  if (fd < 0) {
    return -1;
  }
  sys_fchdir(fd);
  sys_close(fd);
  return ret;
}

int sys_brk(u32 end) {
  thread_t* current = thread_current();
#ifdef LOG_BRK
  log_debug("sys brk tid:%x addr:%x\n", current->id, end);
#endif
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
#ifdef LOG_BRK
    log_debug("sys brk return first addr:%x\n", end);
#endif
    return end;
  }
  int size = end - (u32)vm->alloc_addr;
  if (end < vm->alloc_addr) {
    // todo free map age
    log_debug("brk free %x %d\n", end, -size);
    vfree(end, -size);
  }
  int addr = end;
  vm->alloc_size += size;
  vm->alloc_addr = end;
#ifdef LOG_BRK
  log_debug("sys brk return alloc addr:%x\n", addr);
#endif
  return addr;
}

void* sys_mmap2(void* addr, size_t length, int prot, int flags, int fd,
                size_t pgoffset) {
  int ret = 0;
  thread_t* current = thread_current();
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

  void* start_addr = addr;

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
    // 固定地址，说明地址存在，不需要处理
    vmemory_area_t* findvm = vmemory_area_find(current->vm->vma, addr, length);
    if (findvm == NULL) {
      log_error("map fix %x faild out of range\n", start_addr);
      return MAP_FAILED;
    }
#ifdef LOG_MMAP
    log_debug("map fix return addr %x\n", start_addr);
#endif
    return start_addr;
  }

  if (vm->child == NULL) {  // 未分配过
    // 从父亲的中间位置开始拿
    start_addr = vm->vaddr + vm->size / 2;
    vm->child = vmemory_area_create(start_addr, length, MEMORY_MMAP);
  } else {
    // 找下一个内存地址
    vmemory_area_t* last_area = vmemory_area_find_last(vm->child);
    start_addr = last_area->vend;
    vmemory_area_t* new_area =
        vmemory_area_create(start_addr, length, MEMORY_MMAP);
    last_area->next = new_area;
  }

  // 匿名内存
  if ((flags & MAP_ANON) == MAP_ANON) {
#ifdef LOG_MMAP
    log_debug("map anon return addr %x\n", start_addr);
#endif
    return start_addr;
  } else if ((flags & MAP_ANON) == 0) {
    // 有名
    fd_t* f = thread_find_fd_id(current, fd);
    if (f == NULL) {
      log_error("sys mmap not found fd %d tid %d\n", fd, current->id);
      return MAP_FAILED;
    }
  }
  if ((flags & MAP_SHARED) == MAP_SHARED) {
#ifdef LOG_MMAP
    log_debug("map shared return addr %x\n", start_addr);
#endif
    return start_addr;
  } else if ((flags & MAP_PRIVATE) == MAP_PRIVATE) {
    log_debug("map private return addr %x\n", start_addr);
    return start_addr;
  }
  log_error("map failed end\n");
  return MAP_FAILED;
}

void* sys_mremap(void* old_address, size_t old_size, size_t new_size, int flags,
                 ... /* void *new_address */) {
  thread_t* current = thread_current();
  vmemory_area_t* vm = vmemory_area_find_flag(current->vm->vma, MEMORY_HEAP);
  if (vm == NULL) {
    log_error("sys mremap not found vm\n");
    return MAP_FAILED;
  }

  log_debug("sys mremap old addr %x old size %d new size %d\n", old_address,
            old_size, new_size);

  // 内存大小 对齐 16
  new_size = ALIGN(new_size, MEMORY_ALIGMENT);
  int size = new_size - old_size;

  vmemory_area_t* old_area =
      vmemory_area_find(vm->child, old_address, old_size);
  if (old_area == NULL) {
    log_error("not fount vm area\n");
    return MAP_FAILED;
  }

  if ((flags & MREMAP_MAYMOVE) == MREMAP_MAYMOVE) {
    int addr = NULL;
    if (old_area->next == NULL) {
      old_area->size += size;
      old_area->vend += size;
      addr = old_area->alloc_addr;
    } else {
      vmemory_area_t* last_area = vmemory_area_find_last(vm->child);
      u32 start_addr = last_area->vend;
      vmemory_area_t* new_area =
          vmemory_area_create(start_addr, new_size, MEMORY_MMAP);
      last_area->next = new_area;
      addr = new_area->alloc_addr;
    }
    log_debug("mremap maymove return addr %x\n", addr);
    return addr;
  }

  if ((flags & MREMAP_FIXED) == MREMAP_FIXED) {
    log_debug("mremap fixed return addr %x\n", old_address);
    return old_address;
  }

  if (flags == 0) {
    return old_address;
  }

  return MAP_FAILED;
}

int sys_munmap(void* addr, size_t size) {
#ifdef LOG_MMAP
  log_debug("sys munmap addr: %x size: %d\n", addr, size);
#endif
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
  vmemory_area_free(area);

  return 0;
}

int sys_mprotect(const void* start, size_t len, int prot) {
  int ret = 0;
  log_debug("sys mprotect not impl\n");

  return ret;
}

int sys_rt_sigprocmask(int h, void* set, void* old_set) {
  log_debug("sys sigprocmask not impl\n");
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

int sys_unlink(const char* pathname) {
  log_debug("sys unlink not impl %s\n", pathname);
  return -1;
}

int sys_rename(const char* old, const char* new) {
  log_debug("sys rename not impl %s\n", old);

  return -1;
}

int sys_set_thread_area(void* set) {
  log_debug("sys set thread area not impl \n");
  return 0;
}

int sys_getdents64(unsigned int fd, vdirent_t* dir, unsigned int count) {
  thread_t* current = thread_current();
  fd_t* findfd = thread_find_fd_id(current, fd);
  if (findfd == NULL) {
    log_error("getdents64 not found fd %d\n", fd);
    return 0;
  }
  u32 ret = vreaddir(findfd->data, dir, &findfd->offset, count);
  return ret;
}

int sys_fcntl64(int fd, int cmd, void* arg) {
  log_debug("sys fcntl64 fd: %d cmd: %x flag: %x\n", fd, cmd, arg);
  thread_t* current = thread_current();
  fd_t* findfd = thread_find_fd_id(current, fd);
  if (fd == NULL) {
    log_error("sys fcntl64 not found fd %d\n", fd);
    return 0;
  }
  vnode_t* node = findfd->data;
  if (cmd == F_SETFD) {
    u32 ret = vioctl(node, cmd, arg);
    return fd;
  } else if (cmd == F_DUPFD) {
    u32 ret = sys_dup(fd);

    return ret;
  } else if (cmd == F_GETFL) {
    u32 ret = vioctl(node, cmd, arg);
    return ret;
  } else if (cmd == F_SETFL) {
    u32 ret = vioctl(node, cmd, arg);
    return ret;
  } else {
    log_error("not support cmd %d\n", cmd);
  }

  return 1;
}

int sys_getcwd(char* buf, size_t size) {
  thread_t* current = thread_current();
  if (current == NULL) {
    log_error("current is null\n");
    return -1;
  }
  int ret = 0;
  vfs_t* vfs = current->vfs;
  if (vfs == NULL) {
    return -1;
  }
  if (vfs->pwd != NULL) {
    ret = vfs_path_append(vfs->pwd, "", buf);
  } else {
    buf[0] = 0;
  }
  return ret;
}

int sys_fchdir(int fd) {
  u32 ret = 0;
  thread_t* current = thread_current();
  fd_t* f = thread_find_fd_id(current, fd);
  if (f == NULL) {
    log_error("read not found fd %d tid %d\n", fd, current->id);
    return -1;
  }
  vnode_t* node = f->data;
  if ((node->flags & V_DIRECTORY) == V_DIRECTORY) {
    current->vfs->pwd = node;
  } else {
    log_error("not directory\n");
    return -1;
  }
  return ret;
}

int sys_llseek(int fd, int offset_hi, int offset_lo, off_t* result,
               int whence) {
  int i = sizeof(off_t);
  int offset = sys_seek(fd, offset_hi << 32 | offset_lo, whence);
  *result = offset;
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

int sys_stat(const char* path, struct stat* stat) {
  if (stat == NULL) {
    return -1;
  }
  if (path == NULL) {
    return -1;
  }
  int fd = sys_open(path, 0);
  return sys_fstat(fd, stat);
}

int sys_fstat(int fd, struct stat* stat) {
  if (stat == NULL) {
    return -1;
  }
  if (fd < 0) {
    return -1;
  }
  thread_t* current = thread_current();
  fd_t* f = thread_find_fd_id(current, fd);
  if (f == NULL) {
    log_error("stat not found fd %d tid %d\n", fd, current->id);
    return -1;
  }
  vnode_t* node = f->data;
  u32 cmd = IOC_STAT;
  u32 ret = vioctl(node, cmd, stat);
  return ret;
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
  // kprintf("sys_clock_nanosleep %d %d\n",req->tv_sec,req->tv_nsec);
  schedule_sleep(req->tv_sec * 1000 * 1000 * 1000 + req->tv_nsec);
  return 0;
}

int sys_nanosleep(struct timespec* req, struct timespec* rem) {
  schedule_sleep(req->tv_sec * 1000 * 1000 * 1000 + req->tv_nsec);
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
  if (current->info == NULL) {
    current->info = kmalloc(sizeof(thread_info_t), KERNEL_TYPE);
    current->info->self = current->info;
    current->info->tid = current->id;
    current->info->errno = 0;
    current->info->prev = current->info->next = NULL;
    current->info->locale = kmalloc(sizeof(locale_t), KERNEL_TYPE);
    current->info->robust_list.head = &current->info->robust_list.head;
    log_debug("locale at %x\n", current->info->locale);
    log_debug("thread info at %x\n", current->info);
  }
  // log_debug("sys thread self at %x\n", current->info);
  return TP_ADJ(current->info);
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

uint32_t secs_of_years(int years) {
  uint32_t days = 0;
  years += 2000;
  while (years > 1969) {
    days += 365;
    if (years % 4 == 0) {
      if (years % 100 == 0) {
        if (years % 400 == 0) {
          days++;
        }
      } else {
        days++;
      }
    }
    years--;
  }
  return days * 86400;
}

uint32_t secs_of_month(int months, int year) {
  year += 2000;

  uint32_t days = 0;
  switch (months) {
    case 11:
      days += 30;
    case 10:
      days += 31;
    case 9:
      days += 30;
    case 8:
      days += 31;
    case 7:
      days += 31;
    case 6:
      days += 30;
    case 5:
      days += 31;
    case 4:
      days += 30;
    case 3:
      days += 31;
    case 2:
      days += 28;
      if ((year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0))) {
        days++;
      }
    case 1:
      days += 31;
    default:
      break;
  }
  return days * 86400;
}

u32 sys_time(time_t* t) {
  u32 time_fd = -1;
  if (time_fd == -1) {
    time_fd = sys_open("/dev/time", 0);
  }
  if (time_fd < 0) {
    log_error("open time faild\n");
    return 0;
  }
  rtc_time_t time;
  time.day = 1;
  time.hour = 0;
  time.minute = 0;
  time.month = 1;
  time.second = 0;
  time.year = 1900;
  int ret = sys_read(time_fd, &time, sizeof(rtc_time_t));
  if (ret < 0) {
    return 0;
  }
  uint32_t seconds = secs_of_years(time.year - 1) +
                     secs_of_month(time.month - 1, time.year) +
                     (time.day - 1) * 86400 + time.hour * 3600 +
                     time.minute * 60 + time.second + 0;
  *t = seconds;
  return ret;
}

int sys_clock_gettime64(clockid_t clockid, struct timespec* ts) {
  if (clockid == 1) {
    time_t seconds;
    int rc = sys_time(&seconds);
    ts->tv_sec = seconds;
    ts->tv_nsec = 0;
    return 0;
  } else if (clockid == 0) {
    time_t seconds;
    int rc = sys_time(&seconds);
    ts->tv_sec = seconds;
    ts->tv_nsec = 0;
    return 0;
  }else{
    log_warn("clock not support %d\n",clockid);
  }
  return 0;
}

int sys_set_tid_adress(void* ptr) {
  thread_t* current = thread_current();
  log_debug("sys set tid adress not impl\n");
  return current->id;
}

void sys_exit_group(int status) {
  sys_exit(status);
  log_debug("sys exit group not impl\n");
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
  log_debug("sys futext not impl\n");

  return 0;
}

int sys_mkdir(const char* pathname, mode_t mode) {
  log_debug("sys mkdir not impl %s\n", pathname);

  return 0;
}

int sys_access(const char* pathname, int mode) {
  log_debug("sys access not impl %s\n", pathname);
  if (pathname == NULL) {
    return -1;
  }
  int fd = sys_open(pathname, 0);
  if (fd < 0) {
    log_error("access faild %s\n", pathname);
    return -1;
  }
  sys_close(fd);
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

int sys_fstat64(int fd, struct stat* stat) { return sys_fstat(fd, stat); }

int sys_statfs64(const char* filename, struct statfs* stat) {
  if (stat == NULL) {
    return -1;
  }
  thread_t* current = thread_current();
  int f = thread_find_fd_name(current, filename);
  if (f < 0) {
    f = sys_open(filename, 0);
  }
  if (f < 0) {
    log_error("statfs not found name %s tid %d\n", filename, current->id);
    return -1;
  }
  fd_t* fd = thread_find_fd_id(current, f);
  if (fd == NULL) {
    log_error("statfs fd not found name %s tid %d\n", filename, current->id);
    return 0;
  }
  vnode_t* node = fd->data;
  u32 cmd = IOC_STATFS;
  u32 ret = vioctl(node, cmd, stat);
  return ret;
}

int sys_fn_faild_handler(int no, interrupt_context_t* ic) {
  int call_id = context_fn(ic);
  log_debug("sys fn faild %x\n", call_id);
  if (call_id == 0xf0005) {
    return sys_fn_call(ic, &sys_set_thread_area);
  }
}

void sys_fn_call_handler(int no, interrupt_context_t* ic) {
  void* fn = syscall_table[context_fn(ic)];
  if (fn != NULL) {
    // kprintf("syscall fn:%d r0:%x r1:%x r2:%x
    // r3:%x\n",ic->r7,ic->r0,ic->r1,ic->r2,ic->r3);
    sys_fn_call((ic), fn);
    // kprintf(" ret=%x\n",context_ret(ic));
  } else {
    log_warn("syscall %d not found\n", context_fn(ic));
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
  syscall_table[SYS_OPEN] = &sys_open;
  syscall_table[SYS_CLOSE] = &sys_close;
  syscall_table[SYS_DEV_READ] = &dev_read;
  syscall_table[SYS_DEV_WRITE] = &dev_write;
  syscall_table[SYS_DEV_IOCTL] = &dev_ioctl;
  syscall_table[SYS_EXEC] = &sys_exec;
  syscall_table[SYS_TEST] = &sys_test;
  syscall_table[SYS_EXIT] = &sys_exit;
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
  syscall_table[SYS_UNLINK] = &sys_unlink;
  syscall_table[SYS_RENAME] = &sys_rename;

  syscall_table[SYS_RENAME] = &sys_rename;

  syscall_table[SYS_SET_THREAD_AREA] = &sys_set_thread_area;
  syscall_table[SYS_DUMPS] = &sys_dumps;

  syscall_table[SYS_GETDENTS64] = &sys_getdents64;
  syscall_table[SYS_MUNMAP] = &sys_munmap;

  syscall_table[SYS_FCNT64] = &sys_fcntl64;
  syscall_table[SYS_GETCWD] = &sys_getcwd;
  syscall_table[SYS_CHDIR] = &sys_chdir;
  syscall_table[SYS_FCHDIR] = &sys_fchdir;
  syscall_table[SYS_CLONE] = &sys_clone;
  syscall_table[SYS_LLSEEK] = &sys_llseek;

  syscall_table[SYS_UMASK] = &sys_umask;

  syscall_table[SYS_STAT] = &sys_stat;
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
  syscall_table[SYS_READLINK] = &sys_readlink;

  syscall_table[SYS_MADVISE] = &sys_madvice;

  syscall_table[SYS_THREAD_CREATE] = &sys_thread_create;
  syscall_table[SYS_THREAD_DUMP] = &sys_thread_dump;

  syscall_table[SYS_KILL] = &sys_kill;
  syscall_table[SYS_THREAD_ADDR] = &sys_thread_addr;

  syscall_table[SYS_FUTEX] = &sys_futex;
  syscall_table[SYS_MKDIR] = &sys_mkdir;
  syscall_table[SYS_ACESS] = &sys_access;

  syscall_table[SYS_GETTID] = &sys_gettid;
  syscall_table[SYS_THREAD_MAP] = &sys_thread_map;
  syscall_table[SYS_FSTAT64] = &sys_fstat64;
  syscall_table[SYS_STATFS64] = &sys_statfs64;
}