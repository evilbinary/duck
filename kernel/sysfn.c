/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/

#include "sysfn.h"

#include "fd.h"
#include "kernel.h"
#include "kernel/elf.h"
#include "loader.h"
#include "thread.h"
#include "vfs.h"

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
  thread_t* current = thread_current();
  if (current == NULL) {
    log_error(" cannot find current thread\n");
    return -1;
  }
  // current pwd
  vnode_t* pwd = NULL;
  if (kstrlen(name) >= 2 && name[0] == '.' && name[1] == '/') {
    pwd = current->vfs->pwd;
    kstrcpy(name, &name[2]);
  }

  int f = thread_find_fd_name(current, name);
  if (f >= 0) {
    fd_t* fd = thread_find_fd_id(current, f);
    fd->offset = 0;
    log_debug("sys open name return : %s fd: %d\n", name, f);
    return f;
  }
  vnode_t* file = vfs_open_attr(pwd, name, attr);
  if (file == NULL) {
    log_error("sys open file %s error, attr %x \n", name, attr);
    return -1;
  }
  fd_t* fd = fd_open(file, DEVICE_TYPE_FILE, name);
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
    log_debug("sys open new name: %s fd:%d fd->id:%d ptr:%x tid:%d\n", name, f,
              fd->id, fd, current->id);
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
  thread_set_fd(current, fd, NULL);
  fd_close(f);
  return 0;
}

size_t sys_write(u32 fd, void* buf, size_t nbytes) {
  thread_t* current = thread_current();
  fd_t* f = thread_find_fd_id(current, fd);
  if (f == NULL) {
    log_error("write not found fd %d tid %d\n", fd, current->id);
    thread_dump_fd(current);
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
  f->offset += ret;
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
  log_debug("sys exit tid %d %s status %d\n", current->id, current->name,
            status);
  while (current == thread_current()) {
    cpu_sti();
  }
  cpu_cli();
}

void* sys_vmap(void* addr, size_t size) {
  thread_t* current = thread_current();
  vmemory_area_t* area = vmemory_area_create(
      current->vmm->vaddr + current->vmm->size, size, MEMORY_HEAP);
  vmemory_area_add(current->vmm, area);
  return area->vaddr;
}

void sys_vumap(void* ptr, size_t size) {
  thread_t* current = thread_current();
  vmemory_area_t* area = vmemory_area_find(current->vmm, ptr, size);
  if (area == NULL) return;
  vmemory_area_free(area);
}

void* sys_valloc(void* addr, size_t size) { return valloc(addr, size); }

void* sys_vheap() {
  thread_t* current = thread_current();
  return current->vmm->alloc_addr;
}

void sys_vfree(void* addr) {
  // todo
  vfree(addr, PAGE_SIZE);
}

u32 sys_exec(char* filename, char* const argv[], char* const envp[]) {
  thread_t* current = thread_current();
  current->name = filename;
  char* name = kmalloc(kstrlen(filename), KERNEL_TYPE);
  kstrcpy(name, filename);
  current->name = name;

  int fd = sys_open(filename, 0);
  if (fd < 0) {
    log_error("sys exec file not found %s\n", filename);
    return -1;
  }
  fd_t* f = thread_find_fd_id(current, fd);
  if (f == NULL) {
    log_error("read not found fd %d tid %d\n", fd, current->id);
    return 0;
  }
  thread_set_entry(current, (u32*)&run_elf_thread);
  vnode_t* node = f->data;
  if (node == NULL) {
    log_error("sys exec node is null pwd\n");
    return -1;
  }
  sys_close(fd);
  if (node->parent != NULL) {
    current->vfs->pwd = node->parent;
  } else {
    current->vfs->pwd = node;
  }

  // init data
  int argc = 0;
  while (argv != NULL && argv[argc] != NULL) {
    argc++;
  }

  exec_t* data = kmalloc(sizeof(exec_t), KERNEL_TYPE);
  data->filename = filename;
  data->argv = argv;
  data->argc = argc;
  data->envp = envp;
  current->exec = data;
  // thread_set_arg(t, data);
  thread_run(current);

  kmemmove(current->context.ic, current->context.ksp,
           sizeof(interrupt_context_t));

  return 0;
}

int sys_clone(void* fn, void* stack, void* arg) {
  thread_t* current = thread_current();
  if (current == NULL) {
    log_error("current is null\n");
    return -1;
  }
  thread_t* copy_thread = thread_copy(current, THREAD_FORK);
#ifdef LOG_DEBUG
  kprintf("-------dump current thread %d %s-------------\n", current->id);
  thread_dump(current, DUMP_DEFAULT | DUMP_CONTEXT);
  kprintf("-------dump clone thread %d-------------\n", copy_thread->id);
  thread_dump(copy_thread, DUMP_DEFAULT | DUMP_CONTEXT);
#endif

  thread_set_ret(copy_thread, 0);
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

// #define LOG_DEBUG 1

int sys_fork() {
  thread_t* current = thread_current();
  if (current == NULL) {
    log_error("current is null\n");
    return -1;
  }
  thread_stop(current);
  thread_t* copy_thread = thread_copy(current, THREAD_FORK);
  thread_set_ret(copy_thread, 0);
#ifdef LOG_DEBUG
  log_debug("-------dump current thread %d %s-------------\n", current->id);
  thread_dump(current, DUMP_DEFAULT | DUMP_CONTEXT);
  log_debug("-------dump clone thread %d-------------\n", copy_thread->id);
  thread_dump(copy_thread, DUMP_DEFAULT | DUMP_CONTEXT);
#endif
  thread_run(copy_thread);
  thread_run(current);
  return current->id;
}

int sys_pipe(int fds[2]) {
  thread_t* current = thread_current();
  vnode_t* node = pipe_make(PAGE_SIZE);
  fd_t* fd0 = fd_open(node, DEVICE_TYPE_VIRTUAL, "pipe0");
  fd_t* fd1 = fd_open(node, DEVICE_TYPE_VIRTUAL, "pipe1");
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
  u32 ret = vreaddir(findfd->data, dirent, index);
  return ret;
}

int sys_readv(int fd, iovec_t* vector, int count) {
  int ret = 0;
  int n;
  int i;
  for (i = 0; i < count; i++, vector++) {
    n = sys_read(fd, vector->iov_base, vector->iov_len);
    if (n < 0) return n;
    ret += n;
    if (n != vector->iov_len) break;
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
  log_debug("sys brk tid:%x addr:%x\n", current->id, end);

  vmemory_area_t* vm = vmemory_area_find_flag(current->vmm, MEMORY_HEAP);
  if (vm == NULL) {
    log_error("sys brk not found vm\n");
    return -1;
  }
  if (end == 0) {
    if (vm->alloc_addr == 0) {
      vm->alloc_addr = vm->vaddr + end;
    }
    end = vm->alloc_addr;
    log_debug("sys brk return first addr:%x\n", end);
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

  log_debug("sys brk return alloc addr:%x\n", addr);
  return addr;
}

void* sys_mmap2(void* addr, size_t length, int prot, int flags, int fd,
                size_t pgoffset) {
  int ret = 0;
  thread_t* current = thread_current();
  vmemory_area_t* vm = vmemory_area_find_flag(current->vmm, MEMORY_HEAP);
  if (vm == NULL) {
    log_error("sys mmap2 not found vm\n");
    return MAP_FAILED;
  }
  log_debug(
      "sys mmap2 addr=%x length=%d prot = %x,flags = %x, fd = %d, "
      "pgoffset = %d "
      "\n",
      addr, length, prot, flags, fd, pgoffset);

  if (length <= 0) {
    log_error("map failed length 0\n");
    return MAP_FAILED;
  }

  // 内存大小 对齐 16 page-aligned
  length = ALIGN(length, PAGE_SIZE);

  void* start_addr = addr;

  if ((flags & MAP_FIXED) == MAP_FIXED) {
    // 固定地址，说明地址存在，不需要处理
    log_debug("map fix return addr %x\n", start_addr);
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
    log_debug("map anon return addr %x\n", start_addr);
    return start_addr;
  } else if ((flags & MAP_ANON) == 0) {
    // 有名
    fd_t* f = thread_find_fd_id(current, fd);
    if (f == NULL) {
      log_error("sys mmap not found fd %d tid %d\n", fd, current->id);
      return MAP_FAILED;
    }
  }
  log_error("map failed end\n");
  return MAP_FAILED;
}

void* sys_mremap(void* old_address, size_t old_size, size_t new_size, int flags,
                 ... /* void *new_address */) {
  thread_t* current = thread_current();
  vmemory_area_t* vm = vmemory_area_find_flag(current->vmm, MEMORY_HEAP);
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
  log_debug("sys munmap addr: %x size: %d\n", addr, size);

  thread_t* current = thread_current();
  vmemory_area_t* vm = vmemory_area_find_flag(current->vmm, MEMORY_HEAP);
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
  return 1;
}

int sys_getdents64(unsigned int fd, vdirent_t* dir, unsigned int count) {
  thread_t* current = thread_current();
  fd_t* findfd = thread_find_fd_id(current, fd);
  if (fd == NULL) {
    log_error("getdents64 not found fd %d\n", fd);
    return 0;
  }
  u32 ret = vreaddir(findfd->data, dir, count);
  return ret;
}

int sys_fcntl64(int fd, int cmd, void* arg) {
  log_debug("sys fcntl64 not impl fd: %d cmd: %x\n", fd, cmd);

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
    ret == kstrcpy(buf, vfs->pwd->name);
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
  int fd = sys_open(path, 0);
  return sys_fstat(fd, stat);
}

int sys_fstat(int fd, struct stat* stat) {
  if (stat == NULL) {
    return -1;
  }
  thread_t* current = thread_current();
  fd_t* f = thread_find_fd_id(current, fd);
  if (f == NULL) {
    log_error("stat not found fd %d tid %d\n", fd, current->id);
    return 0;
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
    current->info->locale = kmalloc(sizeof(locale_t), KERNEL_TYPE);
    log_debug("locale at %x\n", current->info->locale);
  }
  return current->info;
}

int sys_statx(int dirfd, const char* restrict pathname, int flags,
              unsigned int mask, struct statx* restrict statxbuf) {
  log_debug("sys statx not impl pathname %s\n", pathname);

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
  if (time_fd < 0) return 0;
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
  }
  return 0;
}

int sys_set_tid_adress(void* ptr) {
  thread_t* current = thread_current();
  log_debug("sys set tid adress not impl\n");
  return current->id;
}

void sys_exit_group(int status) { log_debug("sys exit group not impl\n"); }

void sys_fn_init(void** syscall_table) {
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
}