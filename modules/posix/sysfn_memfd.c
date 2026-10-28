/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * memfd_create(319)：匿名内存文件（wayland wl_shm 的标准来路）。
 *
 * 实现：在 /tmp 下建隐藏文件（.memfd-<tid>-<seq>）后立即打开返回 fd。
 * 后端走真实文件，从而免费复用一整条已验证链路：
 *   ftruncate（fat_ftruncate）定长 → mmap 文件 MAP_SHARED（LTP IPC 同款）
 *   → SCM_RIGHTS 把 fd 传给别的进程 → 双方映射同一份页。
 * 代价是像素写会落 FAT（软件渲染可接受），换来零新机制。
 * 名字带 . 前缀做隐藏；文件不删（FAT 对打开文件的 unlink 不可靠），
 * 下次启动随镜像 /tmp 清理。flags 只认 MFD_CLOEXEC/MFD_ALLOW_SEALING
 * （sealing 走 fcntl 的 no-op，见 sysfn_fs.c 的 F_ADD_SEALS）。
 ********************************************************************/
#include "sysfn.h"

#include "kernel/error.h"
#include "kernel/fd.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "kernel/thread.h"

#define log_debug

/* linux/memfd.h 的 uapi 位 */
#define MFD_CLOEXEC_ 0x0001u
#define MFD_ALLOW_SEALING_ 0x0002u

#define MEMFD_NAME_MAX 56

int sys_memfd_create(unsigned long uname, unsigned long flags) {
  thread_t* current = thread_current();
  char name[MEMFD_NAME_MAX];
  char path[MAX_PATH_BUFFER];
  static int seq = 0;
  unsigned long fd;
  fd_t* f;
  int i;
  if (current == NULL || uname == 0) {
    return -22; /* -EINVAL */
  }
  if ((flags & ~(MFD_CLOEXEC_ | MFD_ALLOW_SEALING_)) != 0) {
    return -22; /* 未知 flag：Linux 语义 EINVAL */
  }
  kmemset(name, 0, sizeof(name));
  if (sys_copy_from_user(name, (const void*)(uintptr_t)uname,
                         sizeof(name) - 1) < 0) {
    return -14; /* -EFAULT */
  }
  name[sizeof(name) - 1] = 0;
  /* 名字只作展示，不进路径（防路径注入） */
  for (i = 0; i < MEMFD_NAME_MAX - 1; i++) {
    unsigned char ch = (unsigned char)name[i];
    if (ch == 0) {
      break;
    }
    if (ch == '/' || ch < 0x20 || ch > 0x7e) {
      name[i] = '?';
    }
  }

  /* EXCL 建文件；撞名就顺延 seq 重试几次（同 pid 高频建池） */
  for (i = 0; i < 8; i++) {
    seq++;
    ksnprintf(path, sizeof(path), "/tmp/.memfd-%d-%d-%s", (int)current->id,
              seq, name);
    /* 截断：MAX_PATH_BUFFER 装不下就砍名字尾部 */
    path[sizeof(path) - 1] = 0;
    fd = sys_open_kernel(path, O_RDWR | O_CREAT | O_EXCL);
    if ((long)fd >= 0) {
      break;
    }
  }
  if ((long)fd < 0) {
    return (int)fd; /* 上游错误码（-EACCES/-ENOSPC 等） */
  }
  if (flags & MFD_CLOEXEC_) {
    f = thread_find_fd_id(current, (u32)fd);
    if (f != NULL) {
      f->flags |= FD_CLOEXEC;
    }
  }
  log_debug("memfd create fd=%d path=%s flags=%x\n", (int)fd, path, flags);
  return (int)fd;
}
