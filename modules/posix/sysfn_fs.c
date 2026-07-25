/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "kernel/devfn.h"
#include "kernel/fd.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "kernel/thread.h"
#include "kernel/vfs.h"
#include "sysfn.h"

#define log_debug 

extern vnode_t* root_node;

int fat_node_path(vnode_t* node, char* buf, size_t bufsz);

static int sys_user_page_mapped(u32 va) {
  thread_t* current = thread_current();
  if (current == NULL || current->vm == NULL) {
    return 0;
  }
  va &= ~(PAGE_SIZE - 1);
  return page_v2p(current->vm->upage, (void*)va) != NULL;
}

static int sys_user_range_mapped(const void* user, size_t size) {
  thread_t* current = thread_current();
  if (current == NULL || current->vm == NULL || user == NULL || size == 0) {
    return 0;
  }
  u32 start = (u32)user & ~(PAGE_SIZE - 1);
  u32 end = ((u32)user + size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  for (u32 va = start; va < end; va += PAGE_SIZE) {
    if (!sys_user_page_mapped(va)) {
      return 0;
    }
  }
  return 1;
}

static int sys_user_path_pointer_valid(const char* user) {
  u32 v = (u32)user;
  if (v < PAGE_SIZE) {
    return 0;
  }
  /* Reject pointers that look like pathname text (e.g. 0x6e6f632f == "/con"). */
  u8 b0 = (u8)v;
  u8 b1 = (u8)(v >> 8);
  u8 b2 = (u8)(v >> 16);
  u8 b3 = (u8)(v >> 24);
  if (b0 >= 0x20 && b0 <= 0x7e && b1 >= 0x20 && b1 <= 0x7e && b2 >= 0x20 &&
      b2 <= 0x7e && b3 >= 0x20 && b3 <= 0x7e) {
    return 0;
  }
  return 1;
}

static int sys_copy_user_string(const char* user, char* kbuf, size_t ksize) {
  thread_t* current = thread_current();
  if (user == NULL || kbuf == NULL || ksize == 0) {
    return -1;
  }
  if (!sys_user_path_pointer_valid(user)) {
    log_error("sys_copy_user_string bad user pointer %x\n", user);
    return -1;
  }
#ifdef VM_ENABLE
  if (current != NULL && current->vm != NULL && current->vm->upage != NULL) {
    context_switch_page(current->ctx, (u32)(uintptr_t)current->vm->upage);
  }
#endif
  /* Only require the pages we actually read; do not pre-check the full kbuf. */
  const char* src = user;
  size_t n = 0;
  u32 last_page = ~0U;
  while (n + 1 < ksize) {
    u32 page = ((u32)(src + n)) & ~(PAGE_SIZE - 1);
    if (page != last_page) {
      if (!sys_user_page_mapped(page)) {
        log_error("sys_copy_user_string unmapped user=%x page=%x\n", user, page);
        return -1;
      }
      last_page = page;
    }
    char c = src[n];
    kbuf[n++] = c;
    if (c == '\0') {
      break;
    }
  }
  kbuf[ksize - 1] = '\0';
  return kbuf[0] == '\0' ? -1 : 0;
}

static int sys_copy_from_user(void* kbuf, const void* user, size_t size) {
  if (kbuf == NULL || user == NULL || size == 0) {
    return -1;
  }

  const u8* src = (const u8*)user;
  u8* dst = (u8*)kbuf;
  size_t done = 0;
  u32 last_page = ~0U;
  while (done < size) {
    u32 page = ((u32)(src + done)) & ~(PAGE_SIZE - 1);
    if (page != last_page) {
      if (!sys_user_page_mapped(page)) {
        return -1;
      }
      last_page = page;
    }
    dst[done] = src[done];
    done++;
  }
  return 0;
}

#if (defined(ARM) || defined(ARMV7) || defined(ARMV7_A) || defined(__arm__)) && \
    !defined(ARM64) && !defined(__aarch64__)
static void sys_user_dcache_sync(const void* user, size_t size) {
  if (user == NULL || size == 0) {
    return;
  }
  u32 start = (u32)user & ~31U;
  u32 end = ((u32)user + size + 31U) & ~31U;
  for (u32 va = start; va < end; va += 32) {
    asm volatile("mcr p15, 0, %0, c7, c14, 1" : : "r"(va) : "memory");
  }
  asm volatile("dsb sy" ::: "memory");
}
#else
static void sys_user_dcache_sync(const void* user, size_t size) {
  (void)user;
  (void)size;
}
#endif

static int sys_copy_to_user(void* user, const void* kbuf, size_t size) {
  if (user == NULL || kbuf == NULL || size == 0) {
    return -1;
  }
  const u8* src = (const u8*)kbuf;
  u8* dst = (u8*)user;
  size_t done = 0;
  u32 last_page = ~0U;
  while (done < size) {
    u32 page = ((u32)(dst + done)) & ~(PAGE_SIZE - 1);
    if (page != last_page) {
      if (!sys_user_page_mapped(page)) {
        return -1;
      }
      last_page = page;
    }
    dst[done] = src[done];
    done++;
  }
  // sys_user_dcache_sync(user, size);
  return 0;
}

#define SYS_DT_DIR 4
#define SYS_DT_REG 8

/* musl struct dirent / getdents64 record layout (arch/generic/bits/dirent.h) */
#define MUSL_DIRENT_INO_OFF   0
#define MUSL_DIRENT_OFF_OFF   8
#define MUSL_DIRENT_RECLEN_OFF 16
#define MUSL_DIRENT_TYPE_OFF  18
#define MUSL_DIRENT_NAME_OFF  19

static u16 sys_dirent64_reclen(const char* name) {
  u32 n = kstrlen(name) + 1;
  return (u16)((MUSL_DIRENT_NAME_OFF + n + 7) & ~7u);
}

static void sys_put_dirent64_record(u8* base, u32 pos, u64 ino, i64 next_off,
                                    u8 type, const char* name) {
  u16 reclen = sys_dirent64_reclen(name);
  u8* rec = base + pos;
  size_t name_room = reclen > MUSL_DIRENT_NAME_OFF
                         ? (size_t)(reclen - MUSL_DIRENT_NAME_OFF)
                         : 0;

  kmemset(rec, 0, reclen);
  *(u64*)(rec + MUSL_DIRENT_INO_OFF) = ino;
  *(i64*)(rec + MUSL_DIRENT_OFF_OFF) = next_off;
  *(u16*)(rec + MUSL_DIRENT_RECLEN_OFF) = reclen;
  rec[MUSL_DIRENT_TYPE_OFF] = type;
  if (name != NULL && name_room > 0) {
    kstrncpy((char*)(rec + MUSL_DIRENT_NAME_OFF), name, name_room);
    rec[reclen - 1] = '\0';
  }
}

static u8 sys_vtype_to_dirent(u8 type) {
  if (type == SYS_DT_DIR || (type & V_DIRECTORY) == V_DIRECTORY) {
    return SYS_DT_DIR;
  }
  return SYS_DT_REG;
}

static void sys_vfs_prepare(vfs_t* vfs) {
  if (vfs == NULL) {
    return;
  }
  if (!vfs_node_is_valid(vfs->root)) {
    if (vfs_node_is_valid(root_node)) {
      vfs->root = root_node;
    } else {
      vfs->root = vfs_find(NULL, "/");
    }
  }
  if (!vfs_node_is_valid(vfs->pwd)) {
    vfs->pwd = vfs->root;
  }
  if (!vfs_node_is_valid(vfs->pwd) && vfs_node_is_valid(root_node)) {
    vfs->root = root_node;
    vfs->pwd = root_node;
  }
}

size_t sys_ioctl(u32 fd, u32 cmd, void* args) {
  u32 ret = 0;
  thread_t* current = thread_current();
  fd_t* f = thread_find_fd_id(current, fd);
  if (f == NULL) {
    // f = find_fd(fd);
  }
  if (f == NULL) {
    log_error("ioctl not found fd %d\n", fd);
    return 0;
  }
  vnode_t* node = f->data;
  if (node == NULL) {
    log_error("sys ioctl node is null tid %d fd %d name %s ptr %x cmd %x\n",
              current != NULL ? current->id : -1, fd,
              f->name != NULL ? f->name : 0, f, cmd);
    return 0;
  }
  ret = vioctl(node, cmd, args);

  // log_debug("sys ioctl fd %d %s cmd %x ret %x\n", fd, f->name, cmd, ret);
  return ret;
}

static int sys_buf_in_kernel(const void* buf, size_t size) {
  thread_t* current = thread_current();
  if (current == NULL || current->vm == NULL || buf == NULL || size == 0) {
    return 0;
  }
  u32 start = (u32)buf & ~(PAGE_SIZE - 1);
  u32 end = ((u32)buf + size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  for (u32 va = start; va < end; va += PAGE_SIZE) {
    if (page_v2p(current->vm->kpage, (void*)va) != NULL) {
      return 1;
    }
  }
  return 0;
}

static u32 sys_open_kpath(const char* name, int attr) {
  if (name == NULL) {
    log_error("open name is null\n");
    return -1;
  }
  if (name[0] == '\0') {
    log_error("open name is empty attr %x\n", attr);
    return -1;
  }
  if ((unsigned char)name[0] < 0x20 && name[0] != '/' && name[0] != '.') {
    log_error("open invalid path start 0x%x attr %x\n", (unsigned char)name[0],
              attr);
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
  if (current->vfs == NULL) {
    current->vfs = kmalloc(sizeof(vfs_t), KERNEL_TYPE);
    if (current->vfs == NULL) {
      return -1;
    }
    kmemset(current->vfs, 0, sizeof(vfs_t));
  }
  sys_vfs_prepare(current->vfs);
  vnode_t* pwd = current->vfs->pwd;
  vnode_t* root = current->vfs->root;

  vnode_t* file = vfs_find_relative(root, pwd, name);
  if (file == NULL && (attr & O_CREAT) == O_CREAT) {
    file = vfs_open_attr(pwd, name, attr);
  }
  if (file == NULL) {
    log_error("sys open file %s error, attr %x \n", name, attr);
    return -1;
  }

  char path_name[256];
  kmemset(path_name, 0, sizeof(path_name));
  if (fat_node_path(file, path_name, sizeof(path_name)) < 0) {
    if (vfs_path_append(file, "", path_name) < 0) {
      log_error("sys open path append failed name=%s file=%x\n", name, file);
      return -1;
    }
  }
  if (path_name[0] == '\0') {
    if (name != NULL && name[0] != '\0') {
      kstrncpy(path_name, name, sizeof(path_name) - 1);
    } else {
      kstrncpy(path_name, "/", sizeof(path_name) - 1);
    }
    path_name[sizeof(path_name) - 1] = '\0';
  }

  log_debug("path name %s to %s\n", name, path_name);

  if (current->fds[STDIN] == NULL) {
    thread_fill_fd(current);
  }

  if ((int)vfs_open(file, attr) < 0) {
    log_error("sys open backend failed %s\n", name);
    return -1;
  }

  fd_t* fd = fd_open(file, DEVICE_TYPE_FILE, path_name);
  if (fd == NULL) {
    log_error(" new fd error\n");
    return -1;
  }
  fd->offset = 0;
  int f = thread_add_fd(current, fd);
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

u32 sys_open_kernel(const char* path, int attr) {
  if (path == NULL) {
    return -1;
  }
  return sys_open_kpath(path, attr);
}

u32 sys_open(char* name, int attr, ...) {
  char kpath[MAX_PATH_BUFFER];
  if (sys_copy_user_string(name, kpath, sizeof(kpath)) < 0) {
    log_error("sys open invalid user path %x attr %x\n", name, attr);
    return -1;
  }
  log_debug("sys open path=%s attr=%x\n", kpath, attr);
  return sys_open_kpath(kpath, attr);
}

// ---------------------------------------------------------------------------
// AArch64 Linux ABI compatibility
//   syscall 56 is openat(dirfd, pathname, flags, mode)
//   There is no legacy open(2) syscall on aarch64.
// Our userspace (musl/busybox) will typically issue openat.
// ---------------------------------------------------------------------------
// Dispatcher for aarch64 syscall 56.
// - Linux aarch64: openat(dirfd, pathname, flags, mode)
// - Some bare-metal/musl ports (or older code) may still issue open(pathname,
// flags, mode)
//   but with syscall number 56.
// Keep both working by inspecting the first argument.
u64 sys_open_dispatch(u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5) {
  (void)a3;
  (void)a4;
  (void)a5;
  // If a0 looks like a small dirfd (including AT_FDCWD=-100), treat as openat.
  // Otherwise treat a0 as pathname pointer (open).
  long s0 = (long)a0;
  const char* pathname = NULL;
  int flags = 0;
  int dirfd = -100;
  int is_openat = 0;

  if (s0 >= -4096 && s0 <= 4096) {
    // openat layout
    dirfd = (int)a0;
    pathname = (const char*)a1;
    flags = (int)a2;
    is_openat = 1;
  } else {
    // open(path, flags, mode)
    pathname = (const char*)a0;
    flags = (int)a1;
    /* 部分调用约定会把 flags/mode 对调。仅当 a1 像 mode、a2 像合理
     * flags（非用户指针）时才交换；否则 O_RDONLY=0 时 a2 残留垃圾会误伤。 */
    if ((u32)a1 <= 07777U && (u32)a2 < 0x100000u &&
        ((u32)a2 & 0xF0000) != 0) {
      flags = (int)a2;
    }
  }

  if (pathname == NULL) {
    log_error("sys_open_dispatch null pathname a0=%lx a1=%lx a2=%lx a3=%lx\n",
              a0, a1, a2, a3);
    return (u64)-1;
  }
  if (!sys_user_path_pointer_valid(pathname)) {
    log_error(
        "sys_open_dispatch bad pathname=%lx a0=%lx a1=%lx openat=%d\n", pathname,
        a0, a1, is_openat);
    return (u64)-1;
  }

  if (is_openat) {
    return (u64)sys_openat(dirfd, pathname, flags, (int)a3);
  }
  return (u64)sys_open((char*)pathname, flags);
}

u64 sys_access_dispatch(u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5) {
  (void)a4;
  (void)a5;
  long s0 = (long)a0;
  const char* pathname = NULL;
  int mode = 0;
  int flags = 0;
  int dirfd = 0;

  if (s0 >= -4096 && s0 <= 4096) {
    dirfd = (int)a0;
    pathname = (const char*)a1;
    mode = (int)a2;
    flags = (int)a3;
    if (pathname == NULL) {
      log_error(
          "sys_access_dispatch faccessat null pathname dirfd=%d mode=%x "
          "flags=%x\n",
          dirfd, mode, flags);
      return (u64)-1;
    }
    log_debug(
        "sys_access_dispatch faccessat dirfd=%d path=%s mode=%x flags=%x\n",
        dirfd, pathname, mode, flags);
    return (u64)sys_faccessat(dirfd, pathname, mode, flags);
  }

  pathname = (const char*)a0;
  mode = (int)a1;
  if (pathname == NULL) {
    log_error("sys_access_dispatch access null pathname mode=%x\n", mode);
    return (u64)-1;
  }
  if (!sys_user_path_pointer_valid(pathname)) {
    log_error("sys_access_dispatch bad pathname=%x mode=%x\n", pathname, mode);
    return (u64)-1;
  }
  log_debug("sys_access_dispatch access pathname=%x mode=%x\n", pathname, mode);
  return (u64)sys_access(pathname, mode);
}

u64 sys_stat_dispatch(u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5) {
  (void)a4;
  (void)a5;
  long s0 = (long)a0;
  const char* pathname = NULL;
  struct stat* statbuf = NULL;
  int dirfd = 0;
  int flags = 0;

  if (s0 >= -4096 && s0 <= 4096) {
    dirfd = (int)a0;
    pathname = (const char*)a1;
    statbuf = (struct stat*)a2;
    flags = (int)a3;
    if (pathname == NULL || statbuf == NULL) {
      log_error(
          "sys_stat_dispatch newfstatat bad args dirfd=%d path=%lx stat=%lx "
          "flags=%x\n",
          dirfd, pathname, statbuf, flags);
      return (u64)-1;
    }
    return (u64)sys_newfstatat(dirfd, pathname, statbuf, flags);
  }

  pathname = (const char*)a0;
  statbuf = (struct stat*)a1;
  if (pathname == NULL || statbuf == NULL) {
    log_error("sys_stat_dispatch stat bad args path=%lx stat=%lx\n", pathname,
              statbuf);
    return (u64)-1;
  }
  return (u64)sys_stat(pathname, statbuf);
}

u64 sys_readlink_dispatch(u64 a0, u64 a1, u64 a2, u64 a3, u64 a4,
                                 u64 a5) {
  (void)a4;
  (void)a5;
  long s0 = (long)a0;
  const char* pathname = NULL;
  char* buf = NULL;
  size_t bufsiz = 0;
  int dirfd = 0;

  if (s0 >= -4096 && s0 <= 4096) {
    dirfd = (int)a0;
    pathname = (const char*)a1;
    buf = (char*)a2;
    bufsiz = (size_t)a3;
    if (pathname == NULL || buf == NULL) {
      log_error(
          "sys_readlink_dispatch readlinkat bad args dirfd=%d path=%lx buf=%lx "
          "size=%lx\n",
          dirfd, pathname, buf, bufsiz);
      return (u64)-1;
    }
    return (u64)sys_readlinkat(dirfd, pathname, buf, bufsiz);
  }

  pathname = (const char*)a0;
  buf = (char*)a1;
  bufsiz = (size_t)a2;
  if (pathname == NULL || buf == NULL) {
    log_error(
        "sys_readlink_dispatch readlink bad args path=%lx buf=%lx size=%lx\n",
        pathname, buf, bufsiz);
    return (u64)-1;
  }
  return (u64)sys_readlink(pathname, buf, bufsiz);
}

u32 sys_openat(int dirfd, const char* pathname, int flags, int mode) {
  (void)mode;
  char kpath[MAX_PATH_BUFFER];
  if (pathname == NULL) {
    log_error("sys_openat null pathname dirfd=%d flags=%x\n", dirfd, flags);
    return -1;
  }
  if (sys_copy_user_string(pathname, kpath, sizeof(kpath)) < 0) {
    log_error("sys_openat invalid user path %x dirfd=%d flags=%x\n", pathname,
              dirfd, flags);
    return -1;
  }
  pathname = kpath;
  if (pathname[0] == '\0') {
    // Some userspace paths for "current directory" end up here as openat with
    // an empty pathname. Treat that as "." for AT_FDCWD-style calls so tools
    // like ls can continue.
    if (dirfd == -100 || dirfd == 0) {
      pathname = ".";
    } else {
      log_error("sys_openat empty pathname dirfd=%d flags=%x\n", dirfd, flags);
      return -1;
    }
  }
  log_debug("sys_openat dirfd=%d path=%s flags=%x\n", dirfd, pathname, flags);
  return sys_open_kpath(pathname, flags);
}

int sys_mkdirat(int dirfd, const char* pathname, mode_t mode) {
  (void)dirfd;
  return sys_mkdir(pathname, mode);
}

int sys_unlinkat(int dirfd, const char* pathname, int flags) {
  (void)dirfd;
  (void)flags;
  return sys_unlink(pathname);
}

int sys_renameat(int olddirfd, const char* oldpath, int newdirfd,
                 const char* newpath) {
  (void)olddirfd;
  (void)newdirfd;
  return sys_rename(oldpath, newpath);
}

int sys_newfstatat(int dirfd, const char* pathname, struct stat* stat,
                   int flags) {
  (void)dirfd;
  (void)flags;
  return sys_stat(pathname, stat);
}

ssize_t sys_readlinkat(int dirfd, const char* restrict pathname,
                       char* restrict buf, size_t bufsiz) {
  (void)dirfd;
  return sys_readlink(pathname, buf, bufsiz);
}

int sys_faccessat(int dirfd, const char* pathname, int mode, int flags) {
  if (pathname == NULL) {
    log_error("sys_faccessat null pathname dirfd=%d mode=%x flags=%x\n", dirfd,
              mode, flags);
    return -1;
  }
  log_debug("sys_faccessat dirfd=%d pathname=%x mode=%x flags=%x\n", dirfd,
            pathname, mode, flags);
  return sys_access(pathname, mode);
}

int sys_close(u32 fd) {
  thread_t* current = thread_current();
  fd_t* f = thread_find_fd_id(current, fd);
  if (f == NULL) {
    log_error("close not found fd %d tid %d\n", fd, current->id);
    return 0;
  }
  // fd_close already calls vclose and sets fd->data = NULL.
  thread_set_fd(current, fd, NULL);
  return fd_close(f);
}

size_t sys_write(u32 fd, void* buf, size_t nbytes) {
  thread_t* current = thread_current();
  // if (current != NULL && current->id > 1 && fd <= STDERR) {
  //   log_debug("sys_write tid=%d fd=%d nbytes=%d\n", current->id, fd, nbytes);
  // }
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
  if (buf == NULL || nbytes == 0) {
    return 0;
  }

  u8* write_buf = (u8*)buf;
  u8* tmp_buf = NULL;
  if ((u32)buf >= PAGE_SIZE && !sys_buf_in_kernel(buf, nbytes)) {
    tmp_buf = (u8*)kmalloc(nbytes, KERNEL_TYPE);
    if (tmp_buf == NULL) {
      return -1;
    }
    if (sys_copy_from_user(tmp_buf, buf, nbytes) < 0) {
      kfree(tmp_buf);
      return -1;
    }
    write_buf = tmp_buf;
    // if (current != NULL && current->id > 1 && fd == 1 && nbytes > 0) {
    //   char dbg[65];
    //   size_t n = nbytes < 64 ? nbytes : 64;
    //   kmemcpy(dbg, tmp_buf, n);
    //   dbg[n] = '\0';
    //   kprintf("sys_write stdout tid=%d nbytes=%u text='%s'\n", current->id,
    //           nbytes, dbg);
    // }
  }

  u32 ret = vwrite(node, f->offset, nbytes, write_buf);
  if (tmp_buf != NULL) {
    kfree(tmp_buf);
  }
  if (ret > 0) {
    f->offset += ret;
  }
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
    if (fd <= STDERR) {
      thread_fill_fd(current);
      f = thread_find_fd_id(current, fd);
      if (f != NULL) {
        node = f->data;
      }
    }
    if (node == NULL) {
      log_error("sys read node is null tid %d fd %d name %s ptr %x\n",
                current->id, fd, f != NULL ? f->name : 0, f);
      return -1;
    }
  }

  u8* read_buf = (u8*)buf;
  u8* tmp_buf = NULL;
  if (buf != NULL && nbytes > 0 && (u32)buf >= PAGE_SIZE) {
    if (!sys_user_range_mapped(buf, nbytes)) {
      if (sys_buf_in_kernel(buf, nbytes)) {
        read_buf = (u8*)buf;
      } else {
        tmp_buf = (u8*)kmalloc(nbytes, KERNEL_TYPE);
        if (tmp_buf == NULL) {
          log_error("sys read tmp alloc failed fd %d nbytes %d\n", fd, nbytes);
          return -1;
        }
        read_buf = tmp_buf;
      }
    }
  }

  u32 ret = vread(node, f->offset, nbytes, read_buf);
  if (ret > 0 && tmp_buf != NULL && buf != NULL) {
    if (sys_copy_to_user(buf, tmp_buf, ret) < 0) {
      ret = 0;
    }
  } else if (ret > 0 && read_buf == (u8*)buf && buf != NULL) {
    sys_user_dcache_sync(buf, ret);
  }
  if (tmp_buf != NULL) {
    kfree(tmp_buf);
  }
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
    return -1;
  }
  if (oldfd == newfd) {
    return newfd;
  }

  fd_t* nfd = thread_find_fd_id(current, newfd);
  if (nfd != NULL && nfd != fd) {
    thread_set_fd(current, newfd, NULL);
    fd_close(nfd);
  }

  if (newfd >= current->fd_size) {
    log_error("dup2 newfd limit %d >= %d\n", newfd, current->fd_size);
    return -1;
  }

  thread_set_fd(current, newfd, fd);
  fd->use_count++;
  if (newfd >= current->fd_number) {
    current->fd_number = newfd + 1;
  }
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
  // kprintf("sys_readv====>%d %x %d\n",fd,vector,count);

  for (i = 0; i < count; i++) {
    // kprintf("sys_read=>i=%d %d %x
    // %d\n",i,fd,vector[pos].iov_base,vector[pos].iov_len);
    int len = vector[pos].iov_len;
    n = sys_read(fd, vector[pos].iov_base, len);
    // kprintf("read %d ret =%d\n",i,n);
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
  if (count <= 0 || vector == NULL) {
    return 0;
  }

  iovec_t* kvec = NULL;
  if ((u32)vector >= PAGE_SIZE &&
      !sys_buf_in_kernel(vector, sizeof(iovec_t) * (u32)count)) {
    kvec = (iovec_t*)kmalloc(sizeof(iovec_t) * (u32)count, KERNEL_TYPE);
    if (kvec == NULL) {
      return -1;
    }
    if (sys_copy_from_user(kvec, vector, sizeof(iovec_t) * (u32)count) < 0) {
      kfree(kvec);
      return -1;
    }
    vector = kvec;
  }

  for (i = 0; i < count; i++) {
    if (vector[i].iov_base == NULL || vector[i].iov_len <= 0) {
      continue;
    }
    n = sys_write(fd, vector[i].iov_base, vector[i].iov_len);
    if (n < 0) {
      if (kvec != NULL) {
        kfree(kvec);
      }
      return n;
    }
    ret += n;
    if (n != (int)vector[i].iov_len) {
      break;
    }
  }
  if (kvec != NULL) {
    kfree(kvec);
  }
  return ret;
}

int sys_chdir(const char* path) {
  int ret = 0;
  thread_t* current = thread_current();
  char kpath[MAX_PATH_BUFFER];

  if (path == NULL) {
    return -1;
  }
  if (sys_copy_user_string(path, kpath, sizeof(kpath)) < 0) {
    log_error("sys_chdir invalid user path %x\n", path);
    return -1;
  }

  log_debug("sys_chdir: path='%s' len=%d\n", kpath, kstrlen(kpath));

  vnode_t* node =
      vfs_find_relative(current->vfs->root, current->vfs->pwd, kpath);
  if (!vfs_node_is_valid(node)) {
    log_error("chdir: cannot find %s\n", kpath);
    return -1;
  }

  // 检查是否是目录
  if ((node->flags & V_DIRECTORY) != V_DIRECTORY) {
    log_error("chdir: not a directory %s\n", kpath);
    return -1;
  }

  current->vfs->pwd = node;
  return ret;
}


int sys_unlink(const char* pathname) {
    log_debug("sys unlink not impl %s\n", pathname);
    return -1;
  }
  
  int sys_rename(const char* old, const char* new) {
    log_debug("sys rename not impl %s\n", old);
  
    return -1;
  }


static void sys_getdents_dump_buf(const char* tag, const u8* buf, u32 nbytes) {
  u32 pos = 0;
  u32 idx = 0;

  kprintf("%s begin total=%u bytes buf=%x\n", tag, nbytes, buf);
  while (pos + MUSL_DIRENT_NAME_OFF + 1 <= nbytes) {
    u16 reclen = *(u16*)(buf + pos + MUSL_DIRENT_RECLEN_OFF);
    const char* name = (const char*)(buf + pos + MUSL_DIRENT_NAME_OFF);
    u8 type = buf[pos + MUSL_DIRENT_TYPE_OFF];

    if (reclen < 24 || pos + reclen > nbytes) {
      kprintf("%s stop idx=%u pos=%u bad reclen=%u\n", tag, idx, pos, reclen);
      break;
    }

    kprintf("  [%u] pos=%u reclen=%u type=%u name='%s'\n", idx, pos, reclen,
            type, name);
    pos += reclen;
    idx++;
    if (idx >= 5) {
      kprintf("  ... (%u more entries)\n", (nbytes > pos) ? 1 : 0);
      break;
    }
  }
  kprintf("%s end parsed=%u\n", tag, idx);
}

static void sys_getdents_dump_user_buf(void* user, u32 nbytes) {

  //sys_getdents_dump_buf("getdents user", (const u8*)user, nbytes);
}

int sys_getdents64(unsigned int fd, vdirent_t* dir, unsigned int count) {
  thread_t* current = thread_current();
  if (current != NULL && current->id > 1) {
    log_debug("sys_getdents64 tid=%d fd=%d bytes=%d\n", current->id, fd, count);
  }
  fd_t* findfd = thread_find_fd_id(current, fd);
  if (findfd == NULL) {
    log_error("getdents64 not found fd %d\n", fd);
    return 0;
  }
  if (dir == NULL || count < 24) {
    return 0;
  }

  u8* kbuf = (u8*)kmalloc(count, KERNEL_TYPE);
  if (kbuf == NULL) {
    return -1;
  }
  kmemset(kbuf, 0, count);

  u32 nbytes = 0;
  u32 entry_idx = 0;
  vdirent_t kdirent;
  while (nbytes + 24 <= count) {
    kmemset(&kdirent, 0, sizeof(kdirent));
    u32 n = vreaddir(findfd->data, &kdirent, &findfd->offset, 1);
    if (n == 0 || kdirent.name[0] == '\0') {
      break;
    }

    u16 reclen = sys_dirent64_reclen(kdirent.name);
    if (nbytes + reclen > count) {
      break;
    }

    sys_put_dirent64_record(kbuf, nbytes, kdirent.ino, (i64)(nbytes + reclen),
                            sys_vtype_to_dirent(kdirent.type), kdirent.name);

    if (current != NULL && current->id > 1) {
      // kprintf("getdents build tid=%d idx=%u vfs='%s' reclen=%u type=%u\n",
      //         current->id, entry_idx, kdirent.name, reclen,
      //         sys_vtype_to_dirent(kdirent.type));
    }

    nbytes += reclen;
    entry_idx++;
  }

  if (nbytes > 0) {
    if (current != NULL && current->id > 1) {
      // sys_getdents_dump_buf("getdents kbuf", kbuf, nbytes);
    }
    if (sys_copy_to_user(dir, kbuf, nbytes) < 0) {
      // kprintf("getdents copy_to_user failed tid=%d nbytes=%u user=%x\n",
      //         current != NULL ? current->id : -1, nbytes, dir);
      kfree(kbuf);
      return -1;
    }
    if (current != NULL && current->id > 1) {
      sys_getdents_dump_user_buf(dir, nbytes);
    }
  }
  kfree(kbuf);

  if (current != NULL && current->id > 1) {
    // kprintf("getdents64 done tid=%d ret=%u entries=%u offset=%u\n", current->id,
    //         nbytes, entry_idx, findfd->offset);
    log_debug("sys_getdents64 tid=%d ret=%d offset=%d\n", current->id, nbytes,
              findfd->offset);
  }
  return (int)nbytes;
}
  
  int sys_fcntl64(int fd, int cmd, void* arg) {
    log_debug("sys fcntl64 fd: %d cmd: %x flag: %x\n", fd, cmd, arg);
    thread_t* current = thread_current();
    fd_t* findfd = thread_find_fd_id(current, fd);
    if (findfd == NULL) {
      log_error("sys fcntl64 not found fd %d\n", fd);
      return -1;
    }
    vnode_t* node = findfd->data;
    if ((cmd == F_GETFL || cmd == F_SETFL) && node == NULL) {
      log_error("sys fcntl64 node is null tid %d fd %d name %s ptr %x cmd %x\n",
                current != NULL ? current->id : -1, fd,
                findfd->name != NULL ? findfd->name : 0, findfd, cmd);
      return -1;
    }
    if (cmd == F_SETFD) {
      findfd->flags = (u32)(uintptr_t)arg;
      return fd;
    } else if (cmd == F_DUPFD) {
      u32 ret = sys_dup(fd);
  
      return ret;
    } else if (cmd == F_GETFD) {
      return findfd->flags;
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
  
  char* sys_getcwd(char* buf, size_t size) {
    thread_t* current = thread_current();
    if (current == NULL) {
      log_error("current is null\n");
      return (char*)-1;
    }
    int ret = 0;
    char tmp[MAX_PATH_BUFFER];
    vfs_t* vfs = current->vfs;
    if (vfs == NULL || buf == NULL || size == 0) {
      return (char*)-1;
    }
    sys_vfs_prepare(vfs);
    kmemset(tmp, 0, sizeof(tmp));
    if (vfs_node_is_valid(vfs->pwd)) {
      ret = vfs_path_append(vfs->pwd, "", tmp);
      if (ret < 0 && vfs_node_is_valid(vfs->root)) {
        log_error("sys_getcwd bad pwd=%x, reset to root\n", vfs->pwd);
        vfs->pwd = vfs->root;
        kmemset(tmp, 0, sizeof(tmp));
        ret = vfs_path_append(vfs->pwd, "", tmp);
      }
    } else {
      ret = -1;
    }
    if (ret < 0 || tmp[0] == '\0') {
      if (size < 2) {
        return (char*)-1;
      }
      tmp[0] = '/';
      tmp[1] = '\0';
      ret = 1;
      if (vfs_node_is_valid(vfs->root)) {
        vfs->pwd = vfs->root;
      }
    } else if (ret == 0) {
      if (size < 2) {
        return (char*)-1;
      }
      tmp[0] = '/';
      tmp[1] = '\0';
      ret = 1;
    }
    if ((size_t)ret + 1 > size) {
      return (char*)-1;
    }
    if (current != NULL && current->id > 1) {
      log_debug("sys_getcwd tid=%d path=%s\n", current->id, tmp);
    }
    {
      if (buf != NULL && sys_user_range_mapped(buf, ret + 1)) {
        kmemcpy(buf, tmp, ret + 1);
      } else {
        log_error("sys_getcwd bad user buf %x\n", buf);
        return (char*)-1;
      }
    }
    return buf;
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
    if (vfs_node_is_valid(node) && (node->flags & V_DIRECTORY) == V_DIRECTORY) {
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


int sys_stat(const char* path, struct stat* stat) {
    if (stat == NULL) {
      return -1;
    }
    if (path == NULL) {
      return -1;
    }
    int fd = sys_open(path, 0);
    if (fd < 0) {
      log_error("open file error %s\n", path);
      return -1;
    }
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
    if (node == NULL) {
      log_error("sys fstat node is null tid %d fd %d name %s ptr %x\n",
                current != NULL ? current->id : -1, fd,
                f->name != NULL ? f->name : 0, f);
      return -1;
    }
    u32 cmd = IOC_STAT;
    u32 ret = vioctl(node, cmd, stat);
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
    if (node == NULL) {
      log_error("sys statfs64 node is null name %s tid %d fd %d fdptr %x\n",
                filename, current != NULL ? current->id : -1, f, fd);
      return -1;
    }
    u32 cmd = IOC_STATFS;
    u32 ret = vioctl(node, cmd, stat);
    return ret;
  }


int sys_mkdir(const char* pathname, mode_t mode) {
    log_debug("sys mkdir not impl %s\n", pathname);
  
    return 0;
  }
  
  int sys_access(const char* pathname, int mode) {
    if (pathname == NULL) {
      log_error("sys_access null pathname\n");
      return -1;
    }
    if (!sys_user_path_pointer_valid(pathname)) {
      log_error("sys_access bad pathname %x mode=%x\n", pathname, mode);
      return -1;
    }
    log_debug("sys_access pathname=%x mode=%x\n", pathname, mode);
    int fd = sys_open((char*)pathname, 0);
    if (fd < 0) {
      log_error("access faild pathname=%x\n", pathname);
      return -1;
    }
    sys_close(fd);
    return 0;
  }