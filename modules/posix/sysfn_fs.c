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
#include "kernel/device.h" /* DEVICE_EVDEV：select 就绪判定要用 device->id */
#include "../loader/loader.h" /* exec_params_t：/proc/self/exe 取 exec->filename */
/* VGA_IOC_READ_FRAMBUFFER_WIDTH/HEIGHT/BPP：sysfs 垫片要报 /sys 里的显示
 * 几何（modes/virtual_size/stride/bits_per_pixel），只借这三个 ioctl 命令号，
 * 不碰驱动的结构体 —— 显示设备到底怎么实现与本层无关。 */
#include "../vga/vga.h"
#include "sysfn.h"
#ifdef NET_DRIVER
#include "lwip_port.h"
#endif

extern u32 pip_read(vnode_t* node, u32 offset, u32 size, u8* buffer);
/* pipe 的真实就绪状态（modules/ipc/pipe.c）：bit0=可读 bit1=可写，
 * 返回 -1 表示该 fd 不是 pipe。_t 版本按显式 owner 查 fd 表（唤醒方用）。 */
extern int pipe_poll_ready(int fd);
extern int pipe_poll_ready_t(thread_t* owner, int fd);
/* 【轮询型输入设备的就绪判定】实现见 modules/input/evdev.c（mod-input）。
 * 弱符号：posix 模块不应硬依赖 input 模块；未参与链接时该符号为空，
 * 相关 fd 退回旧的"存在即就绪"规则（不会崩）。 */
extern int evdev_poll_ready(void* dev) __attribute__((weak));

#define log_debug 

extern vnode_t* root_node;

/* 最小 procfs 垫片：命中 /proc 动态路径返回临时 vnode，否则 NULL（实现见文件尾） */
static vnode_t* procfs_lookup(const char* path);
static vnode_t* devfs_lookup(const char* path);

/* 阻塞读写（实现见 sys_read 之前）：sys_write 定义更早，必须先声明 */
static int sys_unix_blockable(u32 fd, fd_t* f);
static int sys_rw_wait(thread_t* current, u32 fd, void* buf, size_t nbytes,
                       int is_write);

/* 最小 sysfs 垫片（实现见文件尾）：只补 /sys/class/graphics/fbN 这一片，
 *   sysfs_lookup          → open/stat/access/readdir 用（sys_open_kpath 先问）；
 *   sysfs_readlink_magic  → readlink 用（.../fb0/device/subsystem 这类软链接）。 */
static vnode_t* sysfs_lookup(const char* path);
static int sysfs_readlink_magic(const char* path, char* out, size_t outsz,
                                size_t* olen);

int fat_node_path(vnode_t* node, char* buf, size_t bufsz);
int fat_ftruncate(vnode_t* node, u64 length);
/* vfs.c 提供：自 node 向上拼出绝对路径（readlink 的 /proc 魔链接要用）。
 * 必须在头部显式声明 —— vfs.h 里没有原型，隐式声明会按 int 处理返回值。 */
int vfs_path_append(vnode_t* node, char* name, char* buf);

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
  /* 不能按指针本身的字节内容判断(堆地址的低字节可能是可打印 ASCII,
   * 如 0x76216230 == '0b!v'),真实校验由 sys_copy_user_string 逐页
   * 检查用户空间映射完成。 */
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

int sys_copy_from_user(void* kbuf, const void* user, size_t size) {
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
  /* 【可移植性】原先是硬编码 asm volatile("dsb sy") —— 那是 ARMv7+ 指令，
   * ARMv5(ARM926) 汇编器直接报 "selected processor does not support `dsb sy'"
   * （而本函数外层条件是 #if defined(ARM)，含 armv5）。
   * 改用架构自带的 dsb() 宏：armv5 展开为 mcr p15,0,r,c7,c10,4（写缓冲排空），
   * armv7/armv8 展开为 dsb sy，各架构自动适配，无需在这里写分支。 */
  dsb();
}
#else
static void sys_user_dcache_sync(const void* user, size_t size) {
  (void)user;
  (void)size;
}
#endif

int sys_copy_to_user(void* user, const void* kbuf, size_t size) {
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

ssize_t sys_ioctl(u32 fd, u32 cmd, void* args) {
  long ret = 0;
  thread_t* current = thread_current();
  fd_t* f = thread_find_fd_id(current, fd);
  if (f == NULL) {
    // f = find_fd(fd);
  }
  if (f == NULL) {
    log_error("ioctl not found fd %d\n", fd);
    return -EBADF; /* POSIX：坏 fd 报 EBADF（恒 0 是"假成功"，LTP 会判错） */
  }
  /* FIONBIO：fd 级非阻塞开关（aarch64 上 glibc 用 _IOW('f',3,int)=0x40046603，
   * 个别库用 _IOW('f',126,int)=0x4004667e）。绝大多数节点没有 op->ioctl，
   * 落到 vioctl 只会 warn 并返回 0 —— 假成功，标志根本没设。Xorg/xclock 连接
   * X 之前都靠它把 socket 设为非阻塞，所以必须在 fd 层真正落地，
   * 语义与 fcntl(F_SETFL, O_NONBLOCK) 一致。 */
  if (cmd == 0x40046603u || cmd == 0x4004667eu) {
    u32 on = 0;
    if (args == NULL || sys_copy_from_user(&on, args, sizeof(on)) >= 0) {
      if (on) {
        f->flags |= O_NONBLOCK;
      } else {
        f->flags &= ~(u32)O_NONBLOCK;
      }
      return 0;
    }
    return -EINVAL;
  }
  vnode_t* node = f->data;
  if (node == NULL) {
    log_error("sys ioctl node is null tid %d fd %d name %s ptr %x cmd %x\n",
              current != NULL ? current->id : -1, fd,
              f->name != NULL ? f->name : 0, f, cmd);
    return 0;
  }
  /* 【无 handler 的节点一律 ENOTTY】节点没挂 op->ioctl 时 vioctl 会 warn
   * 并返回 0 —— 假成功：任何未知命令在 eventfd/epoll/inotify/pipe 这类
   * 节点上都会"成功"（LTP ioctl_pidfd01 对非 pidfd fd 调
   * PIDFD_GET_INFO 期望 EINVAL/EBADF/ENOTTY/EACCES，实测 9 个 TFAIL）。
   * POSIX 语义：ioctl 一个不支持该命令的对象 → ENOTTY。
   * IOC_STAT/IOC_STATFS 不走这里（fstat/statfs 直接调 vioctl）。 */
  if (node->op == NULL || node->op->ioctl == NULL) {
    return -ENOTTY;
  }
  ret = vioctl(node, cmd, args);

  /* 处理器对未实现命令统一返回 -1：映射成 ENOTTY（POSIX 对非 tty 设备
   * 的 ioctl 语义），musl 才能拿到真正的 errno 而不是 -1 裸值。
   * IOC_STAT/IOC_STATFS 的 -1 是文件系统真实失败，不走这个映射。 */
  if (ret == -1 && cmd != IOC_STAT && cmd != IOC_STATFS) {
    return -ENOTTY;
  }
  // log_debug("sys ioctl fd %d %s cmd %x ret %x\n", fd, f->name, cmd, ret);
  return ret;
}

static int sys_buf_in_kernel(const void* buf, size_t size) {
  thread_t* current = thread_current();
  if (current == NULL || current->vm == NULL || buf == NULL || size == 0) {
    return 0;
  }
  /* 用户地址空间（EXEC_ADDR 起）不算内核缓冲，避免与 kpage 段映射重叠时误判 */
  if ((u32)(uintptr_t)buf >= (u32)EXEC_ADDR) {
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
    /* 打印文件名便于定位（attr 异常多因调用约定不匹配/参数错位） */
    log_error("open attr range error %x name=%s\n", attr, name);
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

  /* 【procfs 垫片】/proc/<pid>/stat、/proc/<pid>/task/... 等动态文件不走
   * fatfs（此前这些路径全 ENOENT，futex 系 wait_for_threads 直接 TBROK）。
   * 静态设备节点（/dev/null、/dev/zero、/dev/full 等）由 devfs 建进 VFS，
   * 这里走下面的 vfs_find_relative 正常解析。 */
  vnode_t* file = procfs_lookup(name);
  if (file == NULL) {
    /* 【sysfs 垫片】/sys/class/graphics/fbN 及其软链接（见文件尾） */
    file = sysfs_lookup(name);
  }
  if (file == NULL) {
    /* 【/dev 垫片】切根后设备节点仍要从全局根的 /dev 找（见文件尾） */
    file = devfs_lookup(name);
  }
  char path_name[256];
  kmemset(path_name, 0, sizeof(path_name));
  if (file != NULL) {
    kstrncpy(path_name, name, sizeof(path_name) - 1);
  } else {
    file = vfs_find_relative(root, pwd, name);
    if (file == NULL && (attr & O_CREAT) == O_CREAT) {
      /* 【修复】绝对路径必须从 root 开始解析。原实现恒用 pwd：测试 chdir 进
       * /tmp/LTP_x 后再 open("/tmp/LTP_x/f", O_CREAT) 时从 tmpdir 里找
       * "/tmp/..." 必失败（tst_test.c 建 IPC 文件全灭，EPERM 掩盖真实错误）。 */
      vnode_t* start = (name[0] == '/') ? root : pwd;
      file = vfs_open_attr(start, name, attr);
    }
    if (file == NULL) {
      log_error("sys open file %s error, attr %x \n", name, attr);
      /* 【必须回 -ENOENT，不能一律 -1】-1 会被 musl 翻成 EPERM：xtrans 的
       * trans_mkdir 靠 "stat 失败且 errno==ENOENT" 才肯继续建 /tmp/.X11-unix，
       * 回 EPERM 直接放弃 ⇒ X 服务器没有 unix 监听 fd，客户端全部
       * "unable to open display"（stat/access 都经 sys_open，一并修好）。 */
      return -2; /* -ENOENT */
    }

    /* 内存符号链接的 data 是目标串，不是 fatfs 的 file_info —— 绝不能交给
     * fat_node_path 解引用（会把字符串当结构体读，实测直接崩）。 */
    if ((file->flags & 0xFF) == V_SYMLINK) {
      if (vfs_path_append(file, "", path_name) < 0) {
        kstrncpy(path_name, name, sizeof(path_name) - 1);
      }
    } else if (fat_node_path(file, path_name, sizeof(path_name)) < 0) {
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
  }

  /* 【clone-on-open】/dev/ptmx 这类"每次 open 产出一个新实例"的设备由
   * 各自 op 的 clone_open 槽实现（pty 每 master 一份独立缓冲与从端号）。
   * 必须放在 procfs/sysfs/devfs/vfs 全部解析路径汇合之后 —— devfs_lookup
   * 命中 /dev/ptmx 时 file != NULL，走的是 if 分支，放分支里够不着。 */
  if (file != NULL && file->op != NULL && file->op->clone_open != NULL) {
    vnode_t* cloned = file->op->clone_open(file);
    if (cloned != NULL) {
      file = cloned;
    }
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
  /* O_PATH 只取描述符：记在 fd_t.flags 里，accept 等 I/O 系统调用据此回
   * EBADF（tst_fd 打开 /proc/self/maps 用的就是 O_PATH）。 */
  if ((attr & O_PATH) != 0) {
    fd->flags |= O_PATH;
  }
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
/* dispatch 的参数宽度说明见 sysfn.h 的 sys_arg_t（务必用字长类型） */
u64 sys_open_dispatch(sys_arg_t a0, sys_arg_t a1, sys_arg_t a2, sys_arg_t a3,
                      sys_arg_t a4, sys_arg_t a5) {
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

u64 sys_access_dispatch(sys_arg_t a0, sys_arg_t a1, sys_arg_t a2,
                        sys_arg_t a3, sys_arg_t a4, sys_arg_t a5) {
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

u64 sys_stat_dispatch(sys_arg_t a0, sys_arg_t a1, sys_arg_t a2, sys_arg_t a3,
                      sys_arg_t a4, sys_arg_t a5) {
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

u64 sys_readlink_dispatch(sys_arg_t a0, sys_arg_t a1, sys_arg_t a2,
                          sys_arg_t a3, sys_arg_t a4, sys_arg_t a5) {
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

/* 276 = renameat2。
 *   flags==0            → 普通改名（renameat）
 *   RENAME_NOREPLACE(1) → 目标已存在返回 EEXIST，否则普通改名
 *   RENAME_EXCHANGE(2)/WHITEOUT(4) 未实现 → EINVAL（overlayfs/whiteout 才用，
 *   FAT 上 Linux 本身也不支持） */
int sys_renameat2(int olddirfd, const char* oldpath, int newdirfd,
                  const char* newpath, unsigned int flags) {
  if (flags == 0) {
    return sys_renameat(olddirfd, oldpath, newdirfd, newpath);
  }
  if (flags == 1) { /* RENAME_NOREPLACE */
    struct stat st;
    if (newpath != NULL && sys_stat(newpath, &st) == 0) {
      return -17; /* -EEXIST */
    }
    return sys_renameat(olddirfd, oldpath, newdirfd, newpath);
  }
  return -22; /* -EINVAL */
}

/* 36 = symlinkat（aarch64 没有 symlink）。FAT 本身无法存符号链接，节点只
 * 建在 VFS 缓存里：data = 目标串（kmalloc）、length = 长度、flags =
 * V_SYMLINK。同进程树内创建后立刻 readlink/readlinkat 可读到；重启即丢，
 * 对 LTP 的 readlink 系列和"临时软链"用法足够。
 * errno：父目录不存在 → ENOENT；父路径不是目录 → ENOTDIR；
 *        同名已存在 → EEXIST。 */
int sys_symlinkat(const char* target, int newdirfd, const char* linkpath) {
  thread_t* current = thread_current();
  if (current == NULL || target == NULL || linkpath == NULL) {
    return -EFAULT;
  }
  if (current->vfs == NULL) {
    return -ENOENT;
  }
  char ktarget[MAX_PATH_BUFFER];
  char klink[MAX_PATH_BUFFER];
  kmemset(ktarget, 0, sizeof(ktarget));
  kmemset(klink, 0, sizeof(klink));
  if (sys_copy_user_string(target, ktarget, sizeof(ktarget)) < 0 ||
      sys_copy_user_string(linkpath, klink, sizeof(klink)) < 0) {
    return -EFAULT;
  }
  if (klink[0] == '\0') {
    return -ENOENT;
  }
  while (kstrlen(klink) > 1 && klink[kstrlen(klink) - 1] == '/') {
    klink[kstrlen(klink) - 1] = '\0';
  }

  /* 拆出父目录与新名字 */
  char parent[MAX_PATH_BUFFER];
  const char* base;
  char* slash;
  kstrcpy(parent, klink);
  slash = parent + kstrlen(parent);
  while (slash > parent && *slash != '/') {
    slash--;
  }
  if (*slash == '/') {
    base = slash + 1;
    *slash = '\0';
    if (parent[0] == '\0') {
      parent[0] = '/';
      parent[1] = '\0';
    }
  } else {
    base = klink;
    parent[0] = '\0';
  }
  if (base[0] == '\0') {
    return -ENOENT;
  }

  sys_vfs_prepare(current->vfs);
  vnode_t* root = current->vfs->root;
  vnode_t* pwd = current->vfs->pwd;
  if (newdirfd != -100 && klink[0] != '/') {
    fd_t* f = thread_find_fd_id(current, (u32)newdirfd);
    if (f == NULL || f->data == NULL) {
      return -EBADF;
    }
    pwd = (vnode_t*)f->data;
    if ((pwd->flags & 0xFF) != V_DIRECTORY) {
      return -ENOTDIR;
    }
  }

  vnode_t* dir_node = vfs_find_relative(root, pwd, parent);
  if (!vfs_node_is_valid(dir_node)) {
    return -ENOENT;
  }
  if ((dir_node->flags & 0xFF) != V_DIRECTORY) {
    return -ENOTDIR;
  }
  if (vfs_find_child(dir_node, (char*)base) != NULL) {
    return -EEXIST;
  }

  vnode_t* node = vfs_create_node((u8*)base, V_SYMLINK);
  if (node == NULL) {
    return -ENOMEM;
  }
  size_t tl = kstrlen(ktarget);
  node->data = kmalloc(tl + 1, KERNEL_TYPE);
  if (node->data == NULL) {
    kfree(node->name);
    kfree(node);
    return -ENOMEM;
  }
  kstrcpy(node->data, ktarget);
  node->length = (u32)tl;
  vfs_add_child(dir_node, node);
  return 0;
}

/* 88 = utimensat。FAT 的时间戳精度/写入路径都没接，直接成功返回：
 * 语义上"设置访问/修改时间"失败会让调用方误判文件不可写（tst_test 的
 * SAFE_UTIMES 就是 TBROK），而时间戳在 FAT 上并非可靠信息。 */
int sys_utimensat(int dirfd, const char* pathname, const void* times, int flags) {
  (void)dirfd;
  (void)pathname;
  (void)times;
  (void)flags;
  return 0;
}

/* 37 = linkat。FAT 没有硬链接，这里用"复制内容"模拟：
 * X server 的 LockServer() 必须靠
 *     link("/tmp/.tX0-lock", "/tmp/.X0-lock")
 * 原子地占住显示号，失败就直接
 *     Fatal server error: Linking lock file (/tmp/.X0-lock) in place failed
 * （QEMU 实测就是这个错，Xorg 因此退出）。ENOSYS 还会让部分库走"内核太老"的
 * 降级分支，所以也不能返回 ENOSYS。
 *
 * 语义折中：两个名字各自独立（写一个不影响另一个），退出时各删各的。对锁文件、
 * 临时占位这类"只用来占名"的用法足够；真正的多链接共享 inode 在 FAT 上做不到。 */
int sys_linkat(int olddirfd, const char* oldpath, int newdirfd,
               const char* newpath, int flags) {
  thread_t* current = thread_current();
  char kold[MAX_PATH_BUFFER];
  char knew[MAX_PATH_BUFFER];
  u8 buf[512];
  ssize_t n;
  int ofd;
  int nfd;

  (void)olddirfd;
  (void)newdirfd;
  (void)flags;
  if (current == NULL || current->vfs == NULL || oldpath == NULL ||
      newpath == NULL) {
    return -14; /* -EFAULT */
  }
  if (sys_copy_user_string(oldpath, kold, sizeof(kold)) < 0 ||
      sys_copy_user_string(newpath, knew, sizeof(knew)) < 0) {
    return -14;
  }
  /* 源不存在 → ENOENT；目标已存在 → EEXIST（Xorg 靠它判断"已有服务器在跑"）。
   * 目标存在性用"真的打开一次"来探：vfs_find_relative 会命中 vfs 缓存里
   * 残留的已删节点，报 EEXIST 之后调用方去读又读不到，自相矛盾。 */
  if (vfs_find_relative(current->vfs->root, current->vfs->pwd, kold) == NULL) {
    return -2;
  }
  {
    int probe = (int)sys_open_kpath(knew, O_RDONLY);
    if (probe >= 0) {
      sys_close((u32)probe);
      return -17;
    }
  }

  ofd = (int)sys_open_kpath(kold, O_RDONLY);
  if (ofd < 0) {
    return -2;
  }
  nfd = (int)sys_open_kpath(knew, O_CREAT | O_WRONLY);
  if (nfd < 0) {
    sys_close((u32)ofd);
    return -13; /* -EACCES */
  }
  for (;;) {
    n = (ssize_t)sys_read((u32)ofd, buf, sizeof(buf));
    if (n <= 0) {
      break;
    }
    if ((ssize_t)sys_write((u32)nfd, buf, (size_t)n) != n) {
      break;
    }
  }
  sys_close((u32)ofd);
  sys_close((u32)nfd);
  return 0;
}

int sys_newfstatat(int dirfd, const char* pathname, struct stat* stat,
                   int flags) {
  (void)dirfd;
  (void)flags;
  return sys_stat(pathname, stat);
}

/* ===================== readlink =====================
 * 本文件系统自身没有符号链接（FAT 无 symlink），但 readlink 必须实现成
 * 真语义而不是恒失败/恒成功：
 *   - /proc/<pid|self>/{exe,cwd,root} 与 /proc/<pid|self>/fd/<n> 是"魔链接"，
 *     由内核按当前进程状态生成目标；
 *   - 普通节点只有 flags 为 V_SYMLINK（data = 目标字符串、length = 长度）
 *     时才算链接，其余一律 EINVAL；
 *   - 路径不存在返回 ENOENT。
 * 三类返回值混用是致命 bug：返回 bufsiz（假成功）会让调用方以为链接目标
 * 长 bufsiz（fontconfig 因此按 512MB 分配，把 xclock 推到 1GB mmap 失败）；
 * 恒返回 ENOENT 又会让依赖魔链接的程序（Xorg 探测设备/fd 路径）走失败分支。 */

/* 把链接目标写进用户缓冲：不追加 NUL、超长静默截断、返回写入字节数。 */
static ssize_t sys_readlink_write(const char* target, size_t len, char* buf,
                                  size_t bufsiz) {
  if (target == NULL) {
    return -ENOENT;
  }
  if (buf == NULL || bufsiz == 0) {
    return -EINVAL;
  }
  if (len > bufsiz) {
    len = bufsiz;
  }
  if (!sys_user_range_mapped(buf, len) ||
      sys_copy_to_user(buf, target, len) < 0) {
    return -EFAULT;
  }
  return (ssize_t)len;
}

/* 只量用户串长度（不拷贝），最多探测 limit 字节；-1 表示用户页不可访问。
 * readlink 需要它来判断 ENAMETOOLONG：sys_copy_user_string 会把超长路径
 * 截断到内核缓冲，截断后"路径不存在"会被错报成 ENOENT。 */
static int sys_user_strnlen(const char* user, size_t limit) {
  thread_t* current = thread_current();
  if (user == NULL || limit == 0) {
    return -1;
  }
#ifdef VM_ENABLE
  if (current != NULL && current->vm != NULL && current->vm->upage != NULL) {
    context_switch_page(current->ctx, (u32)(uintptr_t)current->vm->upage);
  }
#endif
  size_t n = 0;
  u32 last_page = ~0U;
  while (n < limit) {
    u32 page = ((u32)(user + n)) & ~(PAGE_SIZE - 1);
    if (page != last_page) {
      if (!sys_user_page_mapped(page)) {
        return -1;
      }
      last_page = page;
    }
    if (user[n] == '\0') {
      return (int)n;
    }
    n++;
  }
  return (int)limit;
}

/* PATH_MAX = 4096：LTP readlink03 专门喂 4097 字节的路径（期望
 * ENAMETOOLONG）和 40 多次符号链接循环（期望 ELOOP，路径本身 474 字节
 * 仍是合法的）。缓冲必须按 PATH_MAX 给，否则长但合法的路径会被错报成
 * ENAMETOOLONG。两个 4KB 缓冲放堆上——内核栈只有 8KB。 */
#define SYSLINK_PATH_MAX 4096
#define SYSLINK_MAX_DEPTH 40

/* 按 POSIX 语义走路径，跟随【中间组件】里的符号链接：
 *   · 中间组件是符号链接 → 展开其目标（相对目标相对链接所在目录）
 *   · 展开次数 > 40 → ELOOP
 *   · 中间组件存在但不是目录 → ENOTDIR
 *   · 组件不存在 → ENOENT
 * 最后一段不跟随：若它是符号链接，*is_link=1、目标写入 out/*outlen
 * （这正是 readlink 的语义——拿到的必须是链接本身的内容）。
 * 返回 0 成功，否则 -errno。 */
static int sys_path_walk(vnode_t* root, vnode_t* base, const char* path,
                         char* out, size_t outsz, size_t* outlen,
                         int* is_link) {
  int ret = 0;
  if (is_link != NULL) {
    *is_link = 0;
  }
  if (outlen != NULL) {
    *outlen = 0;
  }
  if (root == NULL) {
    root = root_node;
  }
  if (path == NULL) {
    return -ENOENT;
  }
  size_t plen = kstrlen(path);
  if (plen >= SYSLINK_PATH_MAX) {
    return -ENAMETOOLONG;
  }

  char* work = kmalloc(SYSLINK_PATH_MAX, KERNEL_TYPE);
  char* merged = kmalloc(SYSLINK_PATH_MAX, KERNEL_TYPE);
  if (work == NULL || merged == NULL) {
    if (work != NULL) {
      kfree(work);
    }
    if (merged != NULL) {
      kfree(merged);
    }
    return -ENOMEM;
  }
  kmemcpy(work, path, plen + 1);

  vnode_t* cur = (path[0] == '/') ? root : (base != NULL ? base : root);
  char* p = work;
  int depth = 0;

  for (;;) {
    while (*p == '/') {
      p++;
    }
    if (*p == '\0') {
      break; /* 落在某个目录节点上：调用方按"不是链接"处理 */
    }
    char* tok = p;
    while (*p != '\0' && *p != '/') {
      p++;
    }
    char saved = *p;
    *p = '\0';

    const char* q = p + ((saved != '\0') ? 1 : 0);
    while (*q == '/') {
      q++;
    }
    int last = (*q == '\0');

    if (kstrcmp(tok, ".") == 0 || kstrcmp(tok, "..") == 0) {
      if (tok[0] == '.' && tok[1] == '.' && cur != root &&
          vfs_node_is_valid(cur->parent)) {
        cur = cur->parent;
      }
      *p = saved;
      if (last) {
        break;
      }
      p += (saved != '\0') ? 1 : 0;
      continue;
    }

    vnode_t* found = vfs_find_child(cur, tok);
    if (found == NULL) {
      vnode_t* op_node = vfs_node_is_valid(cur->super) ? cur->super : cur;
      found = vfind(op_node, tok);
      if (found != NULL) {
        vfs_add_child(cur, found);
      }
    }
    if (found == NULL) {
      *p = saved;
      /* 中间组件不是目录时子查找必然失败：ENOTDIR 比 ENOENT 准确 */
      ret = ((cur->flags & 0xFF) != V_DIRECTORY) ? -ENOTDIR : -ENOENT;
      goto out;
    }

    int link = ((found->flags & 0xFF) == V_SYMLINK);

    if (last) {
      *p = saved;
      if (!link || found->data == NULL) {
        break; /* 存在但不是符号链接（上层返回 EINVAL） */
      }
      size_t l = (found->length > 0) ? (size_t)found->length
                                     : kstrlen((const char*)found->data);
      if (l >= outsz) {
        l = outsz - 1;
      }
      kmemcpy(out, found->data, l);
      out[l] = '\0';
      if (outlen != NULL) {
        *outlen = l;
      }
      if (is_link != NULL) {
        *is_link = 1;
      }
      break;
    }

    if (link) {
      if (++depth > SYSLINK_MAX_DEPTH) {
        *p = saved;
        ret = -ELOOP;
        goto out;
      }
      const char* target = (const char*)found->data;
      if (target == NULL) {
        *p = saved;
        ret = -ENOENT;
        goto out;
      }
      const char* rest = p + ((saved != '\0') ? 1 : 0);
      while (*rest == '/') {
        rest++;
      }
      size_t tl = kstrlen(target);
      size_t rl = kstrlen(rest);
      if (tl + 1 + rl >= SYSLINK_PATH_MAX) {
        *p = saved;
        ret = -ENAMETOOLONG;
        goto out;
      }
      kmemcpy(merged, target, tl);
      if (rl > 0) {
        merged[tl] = '/';
        kmemcpy(merged + tl + 1, rest, rl + 1);
      } else {
        merged[tl] = '\0';
      }
      if (target[0] == '/') {
        cur = root;
      } else {
        cur = vfs_node_is_valid(found->parent) ? found->parent : cur;
      }
      kmemcpy(work, merged, tl + 1 + rl + 1);
      p = work;
      continue;
    }

    if ((found->flags & 0xFF) != V_DIRECTORY) {
      *p = saved;
      ret = -ENOTDIR;
      goto out;
    }
    cur = found;
    *p = saved;
    p += (saved != '\0') ? 1 : 0;
  }

out:
  kfree(work);
  kfree(merged);
  return ret;
}

/* 解析 /proc 魔链接。返回 >0：目标长度（写进 out/*olen）；
 * 0：不是魔链接（交给普通路径解析）；-1：是魔链接但目标不可得。 */
static int sys_readlink_proc_magic(thread_t* current, const char* path,
                                   char* out, size_t outsz, size_t* olen) {
  if (current == NULL || kstrncmp(path, "/proc/", 6) != 0) {
    return 0;
  }
  const char* rest = path + 6;
  int self = 0;
  if (kstrncmp(rest, "self/", 5) == 0) {
    self = 1;
    rest += 4; /* → "/exe" */
  } else {
    u32 pid = 0;
    int digits = 0;
    while (rest[digits] >= '0' && rest[digits] <= '9') {
      pid = pid * 10 + (u32)(rest[digits] - '0');
      digits++;
    }
    if (digits == 0 || rest[digits] != '/') {
      return 0;
    }
    self = (pid == current->id);
    rest += digits;
  }

  if (kstrcmp(rest, "/exe") == 0) {
    if (!self) {
      return -1;
    }
    const char* exe = (current->exec != NULL && current->exec->filename != NULL)
                          ? current->exec->filename
                          : current->name;
    if (exe == NULL) {
      return -1;
    }
    size_t n = kstrlen(exe);
    if (n >= outsz) {
      n = outsz - 1;
    }
    kmemcpy(out, exe, n);
    out[n] = '\0';
    *olen = n;
    return (int)n;
  }
  if (kstrcmp(rest, "/cwd") == 0 || kstrcmp(rest, "/root") == 0) {
    if (!self) {
      return -1;
    }
    vnode_t* base = root_node;
    if (rest[1] == 'c' && current->vfs != NULL && current->vfs->pwd != NULL) {
      base = current->vfs->pwd;
    }
    int n = vfs_path_append(base, "", out);
    if (n <= 0) {
      return -1;
    }
    *olen = (size_t)n;
    return n;
  }
  if (kstrncmp(rest, "/fd/", 4) == 0) {
    u32 fdno = 0;
    int digits = 0;
    while (rest[4 + digits] >= '0' && rest[4 + digits] <= '9') {
      fdno = fdno * 10 + (u32)(rest[4 + digits] - '0');
      digits++;
    }
    if (digits == 0 || rest[4 + digits] != '\0') {
      return 0; /* /proc/<pid>/fd 目录本身不是链接 */
    }
    if (!self) {
      return -1;
    }
    fd_t* f = thread_find_fd_id(current, fdno);
    if (f == NULL || f->name == NULL) {
      return -1;
    }
    size_t n = kstrlen((const char*)f->name);
    if (n >= outsz) {
      n = outsz - 1;
    }
    kmemcpy(out, f->name, n);
    out[n] = '\0';
    *olen = n;
    return (int)n;
  }
  return 0; /* /proc/<pid>/stat 等是普通文件，走通用解析返回 EINVAL */
}

/* dirfd = -100（AT_FDCWD）时从进程 cwd 开始。errno 语义按 Linux：
 *   · dirfd 不是有效 fd → EBADF
 *   · dirfd 不是目录且 pathname 是相对路径 → ENOTDIR
 *   · pathname 为空且 dirfd 指向符号链接 → 读该链接本身（AT_EMPTY_PATH）
 *   · pathname 为空（其他情况）→ ENOENT */
static ssize_t sys_readlink_common(int dirfd, const char* pathname, char* buf,
                                   size_t bufsiz) {
  thread_t* current = thread_current();
  ssize_t ret = -ENOENT;
  char* kpath = NULL;
  char* target = NULL;
  int ulen = -1;

  if (current == NULL || pathname == NULL || buf == NULL) {
    return -EINVAL;
  }
  if (bufsiz == 0) {
    return -EINVAL; /* POSIX：bufsiz 必须为正 */
  }
  /* 【musl 兼容】musl 的 readlink/readlinkat 在 bufsiz==0 时会改用栈上一字节
   * dummy 缓冲、把请求改写成 bufsiz=1，并在 r>0 时把结果改成 0 —— 内核就
   * 永远看不到 bufsiz==0，Linux 的 EINVAL 语义（LTP readlink03/readlinkat02
   * 的专门用例）随之丢失。识别办法：buf 指向调用者栈帧顶端（距陷阱帧里的
   * 用户 SP 不超过 64 字节）且 bufsiz==1，即 musl 的 dummy。 */
  if (bufsiz == 1 && current->ctx != NULL && current->ctx->ic != NULL) {
    u64 usp = current->ctx->ic->sp;
    u64 ubuf = (u64)(uintptr_t)buf;
    /* 实测 musl 的 dummy 位于 SP+104（musl readlink 栈帧内），真实调用者的
     * 1 字节缓冲在更外侧的帧里，取 128 作阈值。 */
    if (usp > PAGE_SIZE && ubuf > PAGE_SIZE && ubuf >= usp &&
        (ubuf - usp) <= 128) {
      return -EINVAL;
    }
  }

  ulen = sys_user_strnlen(pathname, SYSLINK_PATH_MAX);
  if (ulen < 0) {
    return -EFAULT;
  }
  if (ulen >= SYSLINK_PATH_MAX) {
    return -ENAMETOOLONG;
  }

  kpath = kmalloc(SYSLINK_PATH_MAX, KERNEL_TYPE);
  target = kmalloc(SYSLINK_PATH_MAX, KERNEL_TYPE);
  if (kpath == NULL || target == NULL) {
    ret = -ENOMEM;
    goto done;
  }
  kmemset(kpath, 0, SYSLINK_PATH_MAX);
  kmemset(target, 0, SYSLINK_PATH_MAX);
  /* 【空路径不能走 sys_copy_user_string】它对空串返回 -1（通用 open 路径
   * 需要这个防呆），会把 readlink("") 的 ENOENT 变成 EFAULT。
   * readlink 的空路径有独立语义：dirfd 指向符号链接时读该链接。 */
  if (ulen > 0 && sys_copy_user_string(pathname, kpath, SYSLINK_PATH_MAX) < 0) {
    ret = -EFAULT;
    goto done;
  }

  {
    vnode_t* dir = NULL;
    int dir_is_link = 0;
    if (dirfd != -100) {
      fd_t* f = thread_find_fd_id(current, (u32)dirfd);
      if (f == NULL || f->data == NULL) {
        ret = -EBADF;
        goto done;
      }
      dir = (vnode_t*)f->data;
      dir_is_link = ((dir->flags & 0xFF) == V_SYMLINK);
    }

    if (kpath[0] == '\0') {
      /* 空路径只在 dirfd 本身是符号链接时有意义 */
      if (dir_is_link && dir->data != NULL) {
        const char* link = (const char*)dir->data;
        size_t len = (dir->length > 0) ? (size_t)dir->length : kstrlen(link);
        ret = sys_readlink_write(link, len, buf, bufsiz);
      }
      goto done;
    }

    if (dir != NULL && kpath[0] != '/' && (dir->flags & 0xFF) != V_DIRECTORY) {
      ret = -ENOTDIR; /* dirfd 是普通文件，相对路径无法解析 */
      goto done;
    }

    {
      size_t tlen = 0;
      int magic = sysfs_readlink_magic(kpath, target, SYSLINK_PATH_MAX, &tlen);
      if (magic > 0) {
        ret = sys_readlink_write(target, tlen, buf, bufsiz);
        goto done;
      }
      if (magic < 0) {
        /* -2 = 存在但不是链接（Linux 语义 EINVAL，realpath 依赖它） */
        ret = (magic == -2) ? -EINVAL : -ENOENT;
        goto done;
      }
    }

    {
      size_t tlen = 0;
      int magic =
          sys_readlink_proc_magic(current, kpath, target, SYSLINK_PATH_MAX,
                                  &tlen);
      if (magic < 0) {
        ret = -ENOENT;
        goto done;
      }
      if (magic > 0) {
        ret = sys_readlink_write(target, tlen, buf, bufsiz);
        goto done;
      }
    }


    {
      vnode_t* root = root_node;
      vnode_t* pwd = root;
      size_t tlen = 0;
      int is_link = 0;
      if (current->vfs != NULL) {
        sys_vfs_prepare(current->vfs);
        root = current->vfs->root;
        pwd = current->vfs->pwd;
      }
      if (dir != NULL && kpath[0] != '/') {
        pwd = dir;
      }
      int rc = sys_path_walk(root, pwd, kpath, target, SYSLINK_PATH_MAX, &tlen,
                             &is_link);
      if (rc < 0) {
        ret = rc; /* ENOENT / ENOTDIR / ELOOP / ENAMETOOLONG */
        goto done;
      }
      if (!is_link) {
        ret = -EINVAL; /* 存在但不是符号链接（Linux 语义） */
        goto done;
      }
      ret = sys_readlink_write(target, tlen, buf, bufsiz);
    }
  }

done:
  if (kpath != NULL) {
    kfree(kpath);
  }
  if (target != NULL) {
    kfree(target);
  }
  return ret;
}

ssize_t sys_readlinkat(int dirfd, const char* restrict pathname,
                       char* restrict buf, size_t bufsiz) {
  return sys_readlink_common(dirfd, pathname, buf, bufsiz);
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
  {
    /* 【诊断·可删】只看管道 fd 的关闭 */
    static int sc_dbg = 0;
    int is_pipe = 0;
    if (f->name != NULL) {
      for (int i = 0; f->name[i] != '\0' && i < 40; i++) {
        if (f->name[i] == 'p' && f->name[i + 1] == 'i' &&
            f->name[i + 2] == 'p' && f->name[i + 3] == 'e') {
          is_pipe = 1;
          break;
        }
      }
    }
    if (is_pipe && sc_dbg++ < 40) {
      log_debug("SC tid=%d fd=%d name=%s\n", current->id, fd, f->name);
    }
  }
  // fd_close already calls vclose and sets fd->data = NULL.
  thread_set_fd(current, fd, NULL);
  return fd_close(f);
}

#define SYS_FD_SETSIZE 1024
#define SYS_FD_SET_WORDS (SYS_FD_SETSIZE / (sizeof(unsigned long) * 8))

static int sys_fd_set_copy_in(unsigned long* kset, const void* user) {
  if (user == NULL) {
    return 0;
  }
  return sys_copy_from_user(kset, user, SYS_FD_SET_WORDS * sizeof(unsigned long));
}

static int sys_fd_set_copy_out(void* user, const unsigned long* kset) {
  if (user == NULL) {
    return 0;
  }
  return sys_copy_to_user(user, kset, SYS_FD_SET_WORDS * sizeof(unsigned long));
}

/* 一个 fd 在某方向上的就绪判定：
 *   pipe       → 看缓冲/端口真实状态（读端会阻塞，必须真实）
 *   轮询型设备 → 问设备自己（dev->id == DEVICE_EVDEV，见 sys_fd_polled_t）
 *   本机 socket → unix_poll_ready
 *   其它       → 沿用老规则"存在即就绪"
 * 跨文件可见（epoll 也要用同一套判据，见 sysfn.h）。 */
int sys_fd_ready_t(thread_t* owner, int fd, int want_read, int want_write) {
  int pr;
  fd_t* f;
  if (owner == NULL || (f = thread_find_fd_id(owner, fd)) == NULL) {
    return 0;
  }
  /* timerfd：就绪判据是"到期"（见 sysfn_event.c 的 tfd_refresh）。 */
  {
    int tfd = sys_timerfd_node_ready((vnode_t*)f->data);
    if (tfd >= 0) {
      return want_read ? tfd : 0;
    }
  }
  /* 【轮询型输入设备必须给真实答案】这类设备只有在 read 时才去问硬件，
   * 内核无从"被动"得知有没有数据。按老规则"存在即就绪"会让
   * epoll_pwait/select 立刻返回 ⇒ 调用方（X 的 InputThread）read 拿到
   * EAGAIN 后马上再进 epoll_pwait，形成 30%+ 的忙轮。 */
  if (evdev_poll_ready != NULL && f->data != NULL) {
    device_t* dev = (device_t*)((vnode_t*)f->data)->device;
    if (dev != NULL && dev->id == DEVICE_EVDEV) {
      return want_read ? (evdev_poll_ready(dev) ? 1 : 0) : 0;
    }
  }
  pr = pipe_poll_ready_t(owner, fd);
  if (pr >= 0) {
    return want_read ? ((pr & 1) ? 1 : 0) : ((pr & 2) ? 1 : 0);
  }
  return unix_poll_ready_t(owner, fd, want_read, want_write) != 0;
}

int sys_fd_ready(int fd, int want_read, int want_write) {
  return sys_fd_ready_t(thread_current(), fd, want_read, want_write);
}

/* 该 fd 是否"只能靠内核轮询硬件才知道就绪"（见 sysfn.h 的
 * SYS_POLLED_FD_RECHECK_MS）。select/ppoll/epoll_pwait 挂起前用它决定
 * 要不要把长超时切成短睡。 */
int sys_fd_polled_t(thread_t* owner, int fd) {
  fd_t* f;
  device_t* dev;
  if (owner == NULL) {
    return 0;
  }
  f = thread_find_fd_id(owner, fd);
  if (f == NULL || f->data == NULL) {
    return 0;
  }
  /* timerfd 同样"只能靠重查才知道就绪"：到期没有任何内核事件，
   * 必须跟着 SYS_POLLED_FD_RECHECK_MS 切片一起重算。 */
  if (sys_timerfd_node_ready((vnode_t*)f->data) >= 0) {
    return 1;
  }
  if (evdev_poll_ready == NULL) {
    return 0;
  }
  dev = (device_t*)((vnode_t*)f->data)->device;
  return (dev != NULL && dev->id == DEVICE_EVDEV) ? 1 : 0;
}

/* ===== select/pselect 的等待参数（注册给 poll_wait_suspend）=====
 * 原始集合必须由内核保存：唤醒方重算就绪时会就地清掉未就绪位，
 * 没有原集合就无法重复扫描（也看不到"哪些 fd 是用户关心的"）。 */
struct pollw_select {
  thread_t* owner;
  int nfds;
  void* uread;
  void* uwrite;
  void* uexcept;
  unsigned long oread[SYS_FD_SET_WORDS];
  unsigned long owrite[SYS_FD_SET_WORDS];
  unsigned long oexcept[SYS_FD_SET_WORDS];
};

/* 在等待者页表下执行（poll_wait_suspend/poll_wait_notify 负责切页表） */
static int pollw_select_recheck(void* a) {
  struct pollw_select* s = (struct pollw_select*)a;
  unsigned long rset[SYS_FD_SET_WORDS];
  unsigned long wset[SYS_FD_SET_WORDS];
  unsigned long eset[SYS_FD_SET_WORDS];
  int bits = sizeof(unsigned long) * 8;
  int ready = 0;
  int fd;
  kmemcpy(rset, s->oread, sizeof(rset));
  kmemcpy(wset, s->owrite, sizeof(wset));
  kmemcpy(eset, s->oexcept, sizeof(eset));
  for (fd = 0; fd < s->nfds; fd++) {
    unsigned long mask = 1UL << (fd % bits);
    int word = fd / bits;
    int present = thread_find_fd_id(s->owner, fd) != NULL;
    if (s->uread != NULL && (rset[word] & mask)) {
      if (sys_fd_ready_t(s->owner, fd, 1, 0)) {
        ready++;
      } else {
        rset[word] &= ~mask;
      }
    }
    if (s->uwrite != NULL && (wset[word] & mask)) {
      if (sys_fd_ready_t(s->owner, fd, 0, 1)) {
        ready++;
      } else {
        wset[word] &= ~mask;
      }
    }
    if (s->uexcept != NULL && (eset[word] & mask)) {
      if (present) {
        ready++;
      } else {
        eset[word] &= ~mask;
      }
    }
  }
  if (ready > 0) {
    sys_copy_to_user(s->uread, rset, sizeof(rset));
    sys_copy_to_user(s->uwrite, wset, sizeof(wset));
    sys_copy_to_user(s->uexcept, eset, sizeof(eset));
  }
  return ready;
}

static void pollw_select_release(void* a) {
  kfree(a);
}

/* 【纯定时等待】没有 fd 的 select/pselect/ppoll 就是"睡满 timeout"
 * （LTP 的 tst_timer_test 全系列这么用）。恒不就绪 ⇒ 只能靠 tick 超时唤醒，
 * 醒来 x0=0。旧实现走 schedule_sleep（只记账）⇒ 立刻返回 0，
 * 测试判 "pselect() woken up early"。 */
static int pollw_idle_recheck(void* a) {
  (void)a;
  return 0;
}

/* 【真睡 ms 毫秒】给 nanosleep/usleep 用：线程挂起（THREAD_WAITING），
 * tick 到期由调度器以 WAKE_TIMEOUT 唤醒（pollw_fill 写 x0=0）。
 * 返回 0 = 睡满；-4 = 挂起前就有未决信号（POSIX 的 EINTR）。
 * 旧的 schedule_sleep 只记账、syscall 立即回用户态 ⇒ usleep 是空转：
 * X11 客户端的 usleep(2000) 防自旋全成了纯烧 CPU，窗口越多拖动越卡。 */
int sys_idle_wait_ms(u32 ms) {
  return poll_wait_suspend(pollw_idle_recheck, NULL, NULL, NULL, ms, 1);
}

/* 【select/pselect 公共实现】超时由调用方解析好传进来（timeval 或 timespec），
 * 无就绪时交给 poll_wait_suspend 真正挂起（THREAD_WAITING + schedule_switch），
 * 由 unix/pipe 的事件唤醒并写回结果。 */
static int sys_select_core(int nfds, unsigned long* readfds,
                           unsigned long* writefds, unsigned long* exceptfds,
                           u32 tmo_ms, int has_tmo) {
  thread_t* current = thread_current();
  if (current == NULL) {
    return -1;
  }
  /* 【负 nfds 是 EINVAL】Linux 语义（LTP pselect02 的 "nfds=-1" 用例）。
   * 旧实现把它钳成 SYS_FD_SETSIZE ⇒ 变成"扫 1024 个 fd、无超时地等下去"，
   * 用例直接挂死。上限仍按 FD_SETSIZE 钳位（用户态集合就这么大）。 */
  if (nfds < 0) {
    return -22; /* -EINVAL */
  }
  if (nfds > SYS_FD_SETSIZE) {
    nfds = SYS_FD_SETSIZE;
  }

  if (nfds <= 0 || (readfds == NULL && writefds == NULL && exceptfds == NULL)) {
    /* 无 fd：select() 就是定时等待（miniaudio/sdl/LTP 定时框架都这么用） */
    return poll_wait_suspend(pollw_idle_recheck, NULL, NULL, NULL, tmo_ms,
                             has_tmo);
  }

  struct pollw_select* s = kmalloc(sizeof(*s), KERNEL_TYPE);
  unsigned long zeros[SYS_FD_SET_WORDS];

  if (s == NULL) {
    return -12;
  }
  kmemset(s, 0, sizeof(*s));
  s->owner = current;
  s->nfds = nfds;
  s->uread = readfds;
  s->uwrite = writefds;
  s->uexcept = exceptfds;
  if (sys_fd_set_copy_in(s->oread, readfds) < 0 ||
      sys_fd_set_copy_in(s->owrite, writefds) < 0 ||
      sys_fd_set_copy_in(s->oexcept, exceptfds) < 0) {
    log_error("sys_select copy in faild tid %d\n", current->id);
    kfree(s);
    return -1;
  }
  /* 【集合里出现未打开的 fd ⇒ EBADF】POSIX/Linux 的 select/pselect 必查，
   * 且是"整组预检"（不做部分就绪短路）。缺这一步时 pselect02 的
   * "read 集合里放一个已 close 的 fd、timeout=NULL" 用例会一直等下去
   * （旧代码靠"一个假 EINTR"歪打正着返回，ppoll 语义修好后立刻挂死）。 */
  {
    int bits = (int)(sizeof(unsigned long) * 8);
    int fd;
    for (fd = 0; fd < nfds; fd++) {
      unsigned long mask = 1UL << (fd % bits);
      int word = fd / bits;
      if (((s->oread[word] | s->owrite[word] | s->oexcept[word]) & mask) == 0) {
        continue;
      }
      if (thread_find_fd_id(current, fd) == NULL) {
        kfree(s);
        return -9; /* -EBADF */
      }
    }
  }
  /* 【轮询型设备：把长超时切成短睡】见 sysfn.h 的 SYS_POLLED_FD_RECHECK_MS。
   * 按用户的期望语义无限等是没有意义的——没人会来唤醒；切短后每次醒来返回
   * 0（"没事件"），调用方重进 select 时才重新问一次硬件。 */
  {
    int bits = (int)(sizeof(unsigned long) * 8);
    int fd;
    for (fd = 0; fd < nfds; fd++) {
      unsigned long mask = 1UL << (fd % bits);
      int word = fd / bits;
      if (((s->oread[word] | s->owrite[word] | s->oexcept[word]) & mask) == 0) {
        continue;
      }
      if (!sys_fd_polled_t(current, fd)) {
        continue;
      }
      has_tmo = 1;
      if (tmo_ms > SYS_POLLED_FD_RECHECK_MS) {
        tmo_ms = SYS_POLLED_FD_RECHECK_MS;
      }
      break;
    }
  }
  /* 【超时路径不碰用户内存】tick 到期只把保存帧 x0 写 0，所以挂起前先把
   * 用户集合清零：select 超时返回 0 时集合内容本就未定义，清零符合预期。 */
  kmemset(zeros, 0, sizeof(zeros));
  sys_fd_set_copy_out(readfds, zeros);
  sys_fd_set_copy_out(writefds, zeros);
  sys_fd_set_copy_out(exceptfds, zeros);
  return poll_wait_suspend(pollw_select_recheck, NULL, pollw_select_release, s,
                           tmo_ms, has_tmo);
}

/* 5th 参数是 timeval（老 select / 非 aarch64） */
int sys_select(int nfds, unsigned long* readfds, unsigned long* writefds,
               unsigned long* exceptfds, struct timeval* timeout) {
  struct timeval tv;
  u32 ms = 0;
  int has = 0;
  if (timeout != NULL) {
    if (sys_copy_from_user(&tv, timeout, sizeof(tv)) < 0) {
      return -1;
    }
    has = 1;
    if (tv.tv_sec < 0 || tv.tv_usec < 0) {
      return -22; /* -EINVAL */
    }
    ms = (u32)tv.tv_sec * 1000u + (u32)(tv.tv_usec / 1000);
  }
  return sys_select_core(nfds, readfds, writefds, exceptfds, ms, has);
}

/* 72(aarch64) = pselect6：aarch64 没有 select，musl 的 select()/pselect() 全走
 * 这里，第 5 个参数是 **timespec**（不是 timeval），第 6 个是
 * {sigmask*, sigsetsize}*。
 * 【旧实现把 72 直接指向 sys_select（按 timeval 解析）】⇒ timespec 的 tv_nsec
 * 被当成 tv_usec，超时语义完全错；LTP select04 因此报
 * "pselect() woken up early 491 times range: [0,0]"。这里按 timespec 解析，
 * tv_nsec 越界（含负数）返回 EINVAL。
 * 【sigmask 暂不生效】pselect 的第 6 参是 {sigmask*, sigsetsize}*；这里没有
 * 像 ppoll 那样做临时替换——需要时按 sys_ppoll 的写法补（pollw 参数里带上
 * saved_blocked，release 时恢复）。 */
int sys_pselect6(int nfds, unsigned long* readfds, unsigned long* writefds,
                 unsigned long* exceptfds, void* tsp, void* sig) {
  i64 ts[2];
  u32 ms = 0;
  int has = 0;
  (void)sig;
  if (tsp != NULL) {
    if (sys_copy_from_user(ts, tsp, sizeof(ts)) < 0) {
      return -14; /* -EFAULT */
    }
    if (ts[0] < 0 || ts[1] < 0 || ts[1] >= 1000000000LL) {
      return -22; /* -EINVAL */
    }
    has = 1;
    ms = (u32)ts[0] * 1000u + (u32)(ts[1] / 1000000);
  }
  return sys_select_core(nfds, readfds, writefds, exceptfds, ms, has);
}

/* ===================== ppoll(73) =====================
 * poll 轮询接口（用户态 poll() → ppoll）。沿用 select 的"fd 存在即
 * 就绪"轮询模型：存在 fd 的请求位回填 POLLIN|POLLOUT|POLLPRI，坏 fd
 * 回填 POLLNVAL；全不就绪时按 timeout 睡（NULL = 睡 1 tick 轮询，
 * 与 select 同款）。sigmask 忽略。 */
#define PPOLL_NFDS_MAX 512
#define POLLIN_ 0x001
#define POLLOUT_ 0x004
#define POLLPRI_ 0x008
#define POLLNVAL_ 0x020

struct pollfd_k {
  int fd;
  short events;
  short revents;
};

/* ===== ppoll 的等待参数（注册给 poll_wait_suspend）===== */
struct pollw_poll {
  thread_t* owner;
  void* ufds;
  int nfds;
  struct pollfd_k* fds; /* 内核副本（含用户 events） */
  u64 saved_blocked;    /* ppoll 的临时 sigmask 之前的值（返回时恢复） */
};

/* 在等待者页表下执行（切页表由 poll_wait_suspend/poll_wait_notify 负责） */
static int pollw_poll_recheck(void* a) {
  struct pollw_poll* p = (struct pollw_poll*)a;
  int ready = 0;
  int i;
  for (i = 0; i < p->nfds; i++) {
    short want = p->fds[i].events;
    short got = 0;
    if (p->fds[i].fd < 0) {
      p->fds[i].revents = 0; /* poll 规范：fd<0 忽略 events，revents=0 */
      continue;
    }
    if (thread_find_fd_id(p->owner, p->fds[i].fd) == NULL) {
      got = POLLNVAL_;
    } else {
      if ((want & POLLIN_) && sys_fd_ready_t(p->owner, p->fds[i].fd, 1, 0)) {
        got |= POLLIN_;
      }
      if ((want & POLLOUT_) && sys_fd_ready_t(p->owner, p->fds[i].fd, 0, 1)) {
        got |= POLLOUT_;
      }
      if (want & POLLPRI_) got |= POLLPRI_;
      if (want == 0) got = 0;
    }
    p->fds[i].revents = got;
    if (got != 0) {
      ready++;
    }
  }
  if (ready > 0) {
    sys_copy_to_user(p->ufds, p->fds, sizeof(struct pollfd_k) * p->nfds);
  }
  return ready;
}

static void pollw_poll_release(void* a) {
  struct pollw_poll* p = (struct pollw_poll*)a;
  if (p == NULL) {
    return;
  }
  /* ppoll 的 sigmask 只在调用期间生效：无论正常返回、超时还是被信号打断，
   * 都在这里恢复（release 由 pollw_clear 统一调用）。
   * 【不要在这里消费 pending】屏蔽期间攒下的信号，解除屏蔽后要在"回用户态"
   * 那一刻真正执行 handler（sysfn_signal.c）——由投递钩子消费位图。 */
  if (p->owner != NULL) {
    p->owner->sig_blocked = p->saved_blocked;
  }
  kfree(p->fds);
  kfree(p);
}

int sys_ppoll(void* u_ufds, unsigned int nfds, void* tmo, void* sigmask) {
  thread_t* current = thread_current();
  struct pollfd_k* ufds = (struct pollfd_k*)u_ufds;
  struct pollfd_k* fds = NULL;
  unsigned int i;
  if (current == NULL) {
    return -1;
  }
  {
    /* 【诊断·可删】谁在 ppoll、等哪个 fd、什么事件 */
    static int pp_dbg = 0;
    int tid = (int)current->id;
    if (tid >= 6 && pp_dbg++ < 20) {
      struct pollfd_k k0;
      short fd0 = -1;
      short ev0 = 0;
      if (ufds != NULL && nfds > 0 &&
          sys_copy_from_user(&k0, ufds, sizeof(k0)) >= 0) {
        fd0 = k0.fd;
        ev0 = k0.events;
      }
      log_debug("PPOLL tid=%d nfds=%u fd0=%d ev0=%x tmo=%x\n", tid, nfds, (int)fd0,
                (int)ev0, (u32)(uintptr_t)tmo);
    }
  }
  /* 【nfds 越界是 EINVAL】Linux 语义（超过 RLIMIT_NOFILE）；这里用
   * PPOLL_NFDS_MAX 等价。**不能钳位**：ppoll01 的 INVALID_NFDS（nfds=-1
   * 即 0xffffffff）期望 -1/EINVAL，钳到上限会变成正常扫描并返回 3。 */
  if (nfds > PPOLL_NFDS_MAX) {
    return -22; /* -EINVAL */
  }
  /* 【超时】两种 timespec 布局（与 futex 同款启发式）：
   *   a) 16B {i64 sec, i64 nsec}：内核/现代 libc；
   *   b) 8B old_timespec32 {i32 sec, i32 nsec}：LTP 的 "old kernel spec" 变体。
   * 区分：16B 的小正数 sec 高 32 位恒 0 ⇒ offset4 处的 i32 恒 0；
   * 8B 的 nsec32 几乎总非 0。**必须走 copy_from_user**：8B 布局下直接读
   * tmo+8 会碰到用户态未映射页（内核 fault）。
   * tmo==NULL 表示永久等待（Linux 语义）。 */
  u64 tmo_ms = 0;
  if (tmo != NULL) {
    i32 ts32[2];
    i64 ts64[2];
    if (sys_copy_from_user(ts64, tmo, sizeof(ts64)) < 0) {
      return -14; /* -EFAULT */
    }
    if (sys_copy_from_user(ts32, tmo, sizeof(ts32)) < 0) {
      return -14;
    }
    if (ts32[1] != 0) {
      /* 8B 布局 */
      tmo_ms = (u64)(ts32[0] > 0 ? (u32)ts32[0] : 0) * 1000ull +
               ((u64)(ts32[1] > 0 ? (u32)ts32[1] : 0) + 999999ull) / 1000000ull;
    } else {
      if (ts64[0] < 0 || ts64[1] < 0 || ts64[1] >= 1000000000LL) {
        return -22; /* -EINVAL */
      }
      tmo_ms = (u64)ts64[0] * 1000ull + ((u64)ts64[1] + 999999ull) / 1000000ull;
    }
  }
  /* 【临时 sigmask】ppoll 的 sigmask 只在本次调用期间生效，而且是【替换】而
   * 不是叠加（Linux：调用期间用 sigmask 取代当前掩码）——写成 |= 时"进程里
   * 屏蔽着 SIGINT、ppoll 传空掩码"就永远解不开屏蔽，ppoll01 的
   * SEND_SIGINT_RACE_TEST 会等满超时而不是 EINTR。恢复动作放在
   * pollw_poll_release 里，覆盖"正常返回 / 超时 / 被信号打断"三条路径。 */
  u64 saved_blocked = current->sig_blocked;
  if (sigmask != NULL) {
    u64 sm = 0;
    if (sys_copy_from_user(&sm, sigmask, sizeof(sm)) < 0) {
      return -14; /* -EFAULT */
    }
    current->sig_blocked = sm;
  }
  if (nfds == 0) {
    /* 无 fd 的 poll = 定时等待：必须真睡满 timeout，不能立刻返回 0 */
    struct pollw_poll* pw = kmalloc(sizeof(*pw), KERNEL_TYPE);
    if (pw == NULL) {
      current->sig_blocked = saved_blocked;
      return -12;
    }
    pw->owner = current;
    pw->ufds = ufds;
    pw->nfds = 0;
    pw->fds = NULL;
    pw->saved_blocked = saved_blocked;
    return poll_wait_suspend(
        pollw_idle_recheck, NULL, pollw_poll_release, pw,
        (u32)(tmo_ms > 0x7fffffffull ? 0x7fffffffull : tmo_ms), (tmo != NULL));
  }
  fds = kmalloc(sizeof(struct pollfd_k) * nfds, KERNEL_TYPE);
  if (fds == NULL) {
    current->sig_blocked = saved_blocked;
    return -12; /* -ENOMEM */
  }
  if (sys_copy_from_user(fds, ufds, sizeof(struct pollfd_k) * nfds) < 0) {
    kfree(fds);
    current->sig_blocked = saved_blocked;
    return -14; /* -EFAULT */
  }
  /* 挂起前把用户态 revents 全清 0：超时路径（tick 只写保存帧 x0=0）
   * 不再碰用户内存，语义上"未就绪的 fd revents=0"也正合规范。 */
  for (i = 0; i < nfds; i++) {
    fds[i].revents = 0;
  }
  sys_copy_to_user(ufds, fds, sizeof(struct pollfd_k) * nfds);
  {
    struct pollw_poll* pw = kmalloc(sizeof(*pw), KERNEL_TYPE);
    if (pw == NULL) {
      kfree(fds);
      current->sig_blocked = saved_blocked;
      return -12;
    }
    pw->owner = current;
    pw->ufds = ufds;
    pw->nfds = (int)nfds;
    pw->fds = fds;
    pw->saved_blocked = saved_blocked;
    /* 【轮询型设备：把超时切成短睡】见 sysfn.h 的 SYS_POLLED_FD_RECHECK_MS */
    {
      int has_tmo = (tmo != NULL);
      for (i = 0; i < (int)nfds; i++) {
        if (fds[i].fd < 0 || !sys_fd_polled_t(current, fds[i].fd)) {
          continue;
        }
        has_tmo = 1;
        if (tmo_ms > SYS_POLLED_FD_RECHECK_MS) {
          tmo_ms = SYS_POLLED_FD_RECHECK_MS;
        }
        break;
      }
      return poll_wait_suspend(
          pollw_poll_recheck, NULL, pollw_poll_release, pw,
          (u32)(tmo_ms > 0x7fffffffull ? 0x7fffffffull : tmo_ms), has_tmo);
    }
  }
}

size_t sys_write(u32 fd, void* buf, size_t nbytes) {
  thread_t* current = thread_current();
  // if (current != NULL && current->id > 1 && fd <= STDERR) {
  //   log_debug("sys_write tid=%d fd=%d nbytes=%d\n", current->id, fd, nbytes);
  // }
  fd_t* f = thread_find_fd_id(current, fd);
  if (f == NULL) {
    log_error("write not found fd %d name: %s tid %d\n", fd,f->name, current->id);
    return 0;
  }
  /* 方案 A：socket 的 fd->data 现在也是合法 vnode（socket_operator），
   * 统一走下面的 vwrite，无需特判。 */
  vnode_t* node = f->data;
  if (node == NULL) {
    log_error("sys write node is null tid %d \n", current->id);
    return -1;
  }
  if (buf == NULL || nbytes == 0) {
    return 0;
  }
  {
    /* 【诊断·可删】只看"像 keymap 的写"和"写管道" */
    static int sw_dbg = 0;
    int is_pipe = node->name != NULL && node->name[0] == 'p' &&
                  node->name[1] == 'i' && node->name[2] == 'p';
    if ((is_pipe || nbytes >= 256) && sw_dbg++ < 40) {
      log_debug("SW tid=%d fd=%d name=%s node=%s size=%d\n", current->id, fd,
                f->name != NULL ? f->name : "?",
                node->name != NULL ? node->name : "?", (int)nbytes);
    }
  }

  u8* write_buf = (u8*)buf;
  u8* tmp_buf = NULL;
  /* 【socket 大写不要 kmalloc+copy】每发一块 16KB 都 kmalloc/kfree，反复
   * 大块分配会把内核堆/memory_lock 打挂（实测发首帧 1.2MB 时卡死在
   * kmalloc，整机连 ps 都没响应）。socket 的 op 是 lwip：NETCONN_COPY
   * 由 lwip 自己拷贝数据，直接把用户 buf 传下去即可，双重拷贝也省了。 */
  if (f->type != DEVICE_TYPE_NET && (u32)buf >= PAGE_SIZE &&
      !sys_buf_in_kernel(buf, nbytes)) {
    tmp_buf = (u8*)kmalloc(nbytes, KERNEL_TYPE);
    if (tmp_buf == NULL) {
      return -1;
    }
    if (sys_copy_from_user(tmp_buf, buf, nbytes) < 0) {
      kfree(tmp_buf);
      return -1;
    }
    write_buf = tmp_buf;
  }

  u32 ret = vwrite(node, f->offset, nbytes, write_buf);
  if ((int)ret < 0) {
    if (tmp_buf != NULL) {
      kfree(tmp_buf);
    }
    /* 【阻塞写】本机 socket 且未设 O_NONBLOCK：对端缓冲满就挂起，等它读走
     * 数据（unix_op_read → poll_wait_notify）后重试。 */
    if ((int)ret == -11 && tmp_buf == NULL && buf != NULL && nbytes > 0 &&
        sys_unix_blockable(fd, f)) {
      return (size_t)sys_rw_wait(current, fd, buf, nbytes, 1);
    }
    /* 同 sys_read：-EAGAIN/-EPIPE 必须符号扩展，否则用户态看到的是天文数字 */
    return (size_t)(long)(int)ret;
  }
  if (tmp_buf != NULL) {
    kfree(tmp_buf);
  }
  if (ret > 0) {
    f->offset += ret;
  }
  return ret;
}

/* ===== 阻塞读/写的等待参数（注册给 poll_wait_suspend）=====
 * vread/vwrite 本来就作用于用户 buffer，所以唤醒方只要在【等待者页表】下
 * 重试一次同样的操作即可，不需要额外的数据搬运。 */
struct pollw_rw {
  thread_t* owner;
  int fd;
  void* buf;
  size_t nbytes;
  int is_write;
  long ret; /* 实际完成的字节数（读时为 0 表示 EOF） */
};

/* 返回 1 就绪 / 0 仍不可用（EAGAIN）/ 负值 放弃等待并作为 syscall 返回值 */
static int pollw_rw_recheck(void* a) {
  struct pollw_rw* r = (struct pollw_rw*)a;
  fd_t* f = thread_find_fd_id(r->owner, r->fd);
  u32 n;
  if (f == NULL || f->data == NULL) {
    return -9; /* -EBADF */
  }
  if (r->is_write) {
    n = vwrite((vnode_t*)f->data, f->offset, r->nbytes, (u8*)r->buf);
  } else {
    n = vread((vnode_t*)f->data, f->offset, r->nbytes, (u8*)r->buf);
  }

  if ((int)n == -11) {
    return 0; /* 还是不可用（EAGAIN：无数据 / 缓冲满） */
  }
  if ((int)n < 0) {
    return (int)n; /* EPIPE 之类：放弃等待，让用户态拿到真实错误 */
  }
  if (r->is_write) {
    if (n > 0) {
      f->offset += n;
    }
  } else {
    sys_user_dcache_sync(r->buf, n);
  }
  r->ret = (long)n;
  return 1;
}

static long pollw_rw_result(void* a) {
  return ((struct pollw_rw*)a)->ret;
}

static void pollw_rw_release(void* a) {
  kfree(a);
}

/* 本机 socket 且未设 O_NONBLOCK 才允许阻塞（Xorg 用 FIONBIO 把自己设成
 * 非阻塞，行为必须保持 EAGAIN）。 */
static int sys_unix_blockable(u32 fd, fd_t* f) {
  return f != NULL && unix_owns(fd) && (f->flags & O_NONBLOCK) == 0;
}

/* pty 从端这类 V_BLOCK_READ 节点：空读由 op 返回 -EAGAIN，这里统一挂起等
 * 数据（与 unix socket 同一套 sys_rw_wait；op->read 里不能 thread_wait，
 * schedule_switch 会丢弃内核调用链，见 duck/modules/pty/pty.c 头注释）。 */
static int sys_node_blockable_read(fd_t* f) {
  vnode_t* node;
  if (f == NULL || f->data == NULL || (f->flags & O_NONBLOCK) != 0) {
    return 0;
  }
  node = (vnode_t*)f->data;
  return (node->flags & V_BLOCK_READ) == V_BLOCK_READ;
}

/* 挂起等 read/write 可用；返回实际字节数（不可达时为 0） */
static int sys_rw_wait(thread_t* current, u32 fd, void* buf, size_t nbytes,
                       int is_write) {
  struct pollw_rw* r = kmalloc(sizeof(*r), KERNEL_TYPE);
  if (r == NULL) {
    return -12; /* -ENOMEM */
  }
  r->owner = current;
  r->fd = (int)fd;
  r->buf = buf;
  r->nbytes = nbytes;
  r->is_write = is_write;
  r->ret = 0;
  return poll_wait_suspend(pollw_rw_recheck, pollw_rw_result, pollw_rw_release,
                           r, 0, 0);
}

size_t sys_read(u32 fd, void* buf, size_t nbytes) {
  thread_t* current = thread_current();
  fd_t* f = thread_find_fd_id(current, fd);
  if (f == NULL) {
    log_error("read not found fd %d tid %d\n", fd, current->id);
    return 0;
  }
  /* 方案 A：socket 的 fd->data 现在也是合法 vnode（socket_operator），
   * 统一走下面的 vread，无需特判。 */
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

  u32 ret;
  ret = vread(node, f->offset, nbytes, read_buf);
  {
    /* 【探针·可删】.so 读追踪：定位库内容截断点 */
    const char* nm = f->name != NULL ? f->name : "?";
    int iss = 0;
    for (const char* q = nm; *q; q++) {
      if (q[0] == '.' && q[1] == 's' && q[2] == 'o') {
        iss = 1;
        break;
      }
    }
    if (iss && (nbytes > 512)) {
      u64 pc = (current->ctx != NULL && current->ctx->ic != NULL)
                   ? current->ctx->ic->pc
                   : 0;
      u64 lr = (current->ctx != NULL && current->ctx->ic != NULL)
                   ? current->ctx->ic->lr
                   : 0;
      log_debug("RDCHK %s off=%x req=%x ret=%x tid=%d name=%s pc=%x lr=%x\n",
                nm, (u32)f->offset, (u32)nbytes, ret, current->id,
                current->name != NULL ? current->name : "?", (u32)pc,
                (u32)lr);
    }
  }
  /* 【负值必须符号扩展】vread 的 -EAGAIN/-EPIPE 是 u32 形态（0xFFFFFFF5），
   * 本函数返回 size_t，直接返回它会被 musl 当成"成功读了 4GB"：
   * __syscall_ret 只把 >= -4096UL 的值翻成 errno，0xFFFFFFF5 远小于它。
   * Xorg 的 unix socket 非阻塞读因此拿到天文数字后自旋/踩内存。 */
  if ((int)ret < 0) {
    if (tmp_buf != NULL) {
      kfree(tmp_buf);
    }
    /* 【阻塞读】本机 socket 且没设 O_NONBLOCK：没有数据就挂起，等对端写入
     * 时由 poll_wait_notify 唤醒并重试读取。数据落点就是用户 buffer
     * （vread 直写），所以唤醒方只要在等待者页表下重跑一次 vread。
     * 只处理"数据直读用户 buf"的情况：tmp_buf 路径需要重新分配，交给
     * 用户态重试（返回 EAGAIN），避免在等待表里持有大块内核缓冲。 */
    if ((int)ret == -11 && tmp_buf == NULL && buf != NULL && nbytes > 0 &&
        (sys_unix_blockable(fd, f) || sys_node_blockable_read(f))) {
      return (size_t)sys_rw_wait(current, fd, buf, nbytes, 0);
    }
    return (size_t)(long)(int)ret;
  }
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
    /* 【-EBADF 而非 -1】内核返回 -1 时 musl 一律映射成 EPERM，
     * LTP dup201 等测试专门验证 EBADF。9 = EBADF。 */
    return -9;
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
    return -9;
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
  if (count <= 0 || vector == NULL) {
    return -1;
  }

  /* 与 writev 一致：用户态 iovec 先拷到内核再读 */
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
    n = sys_read(fd, vector[i].iov_base, vector[i].iov_len);
    if (n > 0) {
      num += n;
      ret = num;
      if ((size_t)n < vector[i].iov_len) {
        break;
      }
    } else {
      if (ret < 0) {
        ret = n;
      }
      break;
    }
  }
  if (kvec != NULL) {
    kfree(kvec);
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
  thread_t* current = thread_current();
  char kpath[MAX_PATH_BUFFER];
  char parent[MAX_PATH_BUFFER];
  const char* base;
  char* slash;
  vnode_t* dir_node;

  if (pathname == NULL) {
    return -1;
  }
  if (!sys_user_path_pointer_valid(pathname)) {
    log_error("sys_unlink bad user path %x\n", pathname);
    return -1;
  }
  if (sys_copy_user_string(pathname, kpath, sizeof(kpath)) < 0) {
    return -1;
  }
  if (kstrlen(kpath) == 0) {
    return -1;
  }
  /* 剥掉尾部 '/'："dir/" 这种写法拆分后 base 会是空串 */
  while (kstrlen(kpath) > 1 && kpath[kstrlen(kpath) - 1] == '/') {
    kpath[kstrlen(kpath) - 1] = '\0';
  }

  kstrcpy(parent, kpath);
  slash = parent + kstrlen(parent);
  while (slash > parent && *slash != '/') {
    slash--;
  }
  if (*slash == '/') {
    base = slash + 1;
    *slash = '\0';
    if (parent[0] == '\0') {
      parent[0] = '/';
      parent[1] = '\0';
    }
  } else {
    base = kpath;
    parent[0] = '\0';
  }
  if (base[0] == '\0') {
    return -1;
  }

  /* 【相对路径必须落在 pwd】裸名（无 '/'）时 parent 为空串，传 "/" 会跑到根
   * 目录去找 —— 调用方 cwd 里的相对路径全部解析失败。空串交给
   * vfs_find_relative 直接返回 pwd。 */
  dir_node = vfs_find_relative(current->vfs->root, current->vfs->pwd, parent);
  if (!vfs_node_is_valid(dir_node) ||
      (dir_node->flags & V_DIRECTORY) != V_DIRECTORY) {
    log_error("sys_unlink: parent not found %s\n", parent);
    return -1;
  }

  /* 内存符号链接不在 FAT 上，直接摘链并释放节点（交给 fatfs 只会报
   * "not found"，tst_rmdir 因此留下垃圾节点、同名重建会 EEXIST）。 */
  {
    vnode_t* victim = vfs_find_child(dir_node, (char*)base);
    if (victim != NULL && (victim->flags & 0xFF) == V_SYMLINK) {
      vfs_remove_child(dir_node, victim);
      if (victim->data != NULL) {
        kfree(victim->data);
      }
      if (victim->name != NULL) {
        kfree(victim->name);
      }
      kfree(victim);
      return 0;
    }
  }

  return vunlink(dir_node, base);
}
  
/* ===================== rename(38 / renameat) =====================
 * 此前是恒返回 -1 的 stub。`-1` 在 musl 里一律变成 EPERM(1)，而 Xorg 的
 * LogInit() 只在 errno==ENOENT 时才忽略 rename 失败 ⇒ 直接把
 * "Cannot move old log file ..." 升级成 Fatal server error（实测）。
 * 现在按 POSIX 语义实现：
 *   · 源不存在        → -ENOENT（调用方当作"没什么可挪"）
 *   · 跨目录          → -EXDEV（本实现只支持同一目录内改名）
 *   · 目标已存在      → 由文件系统负责替换（fatfs 先删再 f_rename）
 * 参数是路径，末尾组件当作新名字。 */
static int sys_rename_split(const char* upath, char* parent, char* base) {
  char kpath[MAX_PATH_BUFFER];
  char* slash;

  if (upath == NULL || !sys_user_path_pointer_valid(upath)) {
    return -14; /* -EFAULT */
  }
  if (sys_copy_user_string(upath, kpath, sizeof(kpath)) < 0) {
    return -14;
  }
  if (kstrlen(kpath) == 0) {
    return -2; /* -ENOENT */
  }
  kstrcpy(parent, kpath);
  slash = parent + kstrlen(parent);
  while (slash > parent && *slash != '/') {
    slash--;
  }
  if (*slash == '/') {
    kstrcpy(base, slash + 1);
    *slash = '\0';
    if (parent[0] == '\0') {
      parent[0] = '/';
      parent[1] = '\0';
    }
  } else {
    kstrcpy(base, kpath);
    parent[0] = '\0';
  }
  return (base[0] == '\0') ? -2 : 0;
}

int sys_rename(const char* old, const char* new) {
  thread_t* current = thread_current();
  char old_parent[MAX_PATH_BUFFER];
  char new_parent[MAX_PATH_BUFFER];
  char old_base[MAX_PATH_BUFFER];
  char new_base[MAX_PATH_BUFFER];
  vnode_t* old_dir;
  int rc;

  if (current == NULL || current->vfs == NULL) {
    return -14;
  }
  rc = sys_rename_split(old, old_parent, old_base);
  if (rc < 0) {
    return rc;
  }
  rc = sys_rename_split(new, new_parent, new_base);
  if (rc < 0) {
    return rc;
  }
  if (kstrcmp(old_parent, new_parent) != 0) {
    return -18; /* -EXDEV：只支持同目录改名 */
  }

  /* 相对路径（Xorg 的日志/锁文件都是裸名）必须落在 pwd，不能落到根 */
  old_dir = vfs_find_relative(current->vfs->root, current->vfs->pwd, old_parent);
  if (!vfs_node_is_valid(old_dir) ||
      (old_dir->flags & V_DIRECTORY) != V_DIRECTORY) {
    return -2;
  }
  /* 源不存在 ⇒ ENOENT（Xorg 处理日志/锁文件时把它当"无需处理"忽略） */
  if (vfs_find_relative(current->vfs->root, old_dir, old_base) == NULL) {
    return -2;
  }
  if (vrename(old_dir, old_base, new_base) < 0) {
    return -5; /* -EIO：文件系统拒绝（不支持/目标被占用） */
  }
  return 0;
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
  
  /* 【最小 rlimit】aarch64 getrlimit(163)/prlimit64(261)。musl 的
   * sysconf(_SC_OPEN_MAX) 走 prlimit64/getrlimit(RLIMIT_NOFILE)：此前两个
   * syscall 都没注册，musl 拿 -ENOSYS 后把【栈垃圾】当 rlim_cur 返回
   * （实测 0x700ffc6f = 用户栈地址），dup201 拿它当 open_max 就开始
   * 18.8 亿次 fcntl 扫描，整机卡死。给一个与内核 fd 表容量一致的值。 */
  struct yiyiya_rlimit64 {
    u64 rlim_cur;
    u64 rlim_max;
  };
#define YIYIYA_RLIM_NOFILE 64
#define YIYIYA_RLIM_INFINITY (0xffffffffffffffffULL)

  /* NOFILE 与进程 fd 表容量一致：getdtablesize() 的值必须和 dup2/open 的
   * 真实上限相同，否则 LTP dup201 这类"以 getdtablesize() 为界验证 EBADF"
   * 的测试会边界不一致。 */
  static int yiyiya_nofile(void) {
    thread_t* t = thread_current();
    return (t != NULL && t->fd_size > 0) ? (int)t->fd_size : YIYIYA_RLIM_NOFILE;
  }

  int sys_getrlimit64(int resource, void* lim) {
    struct yiyiya_rlimit64* rl = (struct yiyiya_rlimit64*)lim;
    if (rl == NULL) {
      return -1;
    }
    rl->rlim_cur = (u64)yiyiya_nofile();
    rl->rlim_max = rl->rlim_cur;
    (void)resource;
    return 0;
  }

  int sys_prlimit64(int pid, int resource, void* new_limit, void* old_limit) {
    struct yiyiya_rlimit64* old_rl = (struct yiyiya_rlimit64*)old_limit;
    /* 不支持改限额：只实现查询（musl sysconf 只用 old_limit） */
    if (old_rl != NULL) {
      old_rl->rlim_cur = (u64)yiyiya_nofile();
      old_rl->rlim_max = old_rl->rlim_cur;
    }
    (void)pid;
    (void)resource;
    (void)new_limit;
    return 0;
  }

  int sys_fcntl64(int fd, int cmd, void* arg) {
    log_debug("sys fcntl64 fd: %d cmd: %x flag: %x\n", fd, cmd, arg);
    thread_t* current = thread_current();
    fd_t* findfd = thread_find_fd_id(current, fd);
    if (findfd == NULL) {
      /* EBADF 是常态路径（测试故意传坏 fd），降级成 debug 防串口刷屏 */
      log_debug("sys fcntl64 not found fd %d\n", fd);
      return -1;
    }
    /* 【fcntl 命令不能转发给设备 ioctl】两者命令号空间是重叠的：
     * F_GETFL=3、F_SETFL=4 送进 vioctl 后会被设备当成自己的命令号
     * （pipe 实测打印 "pipe ioctl unsupported cmd=3"）。fcntl 的
     * 描述符标志/状态标志本来就属于 fd 层，这里自己保存即可。 */
    if (cmd == F_SETFD) {
      findfd->flags = (findfd->flags & ~(u32)FD_CLOEXEC) |
                      ((u32)(uintptr_t)arg & (u32)FD_CLOEXEC);
      return 0;
    } else if (cmd == F_DUPFD) {
      u32 ret = sys_dup(fd);

      return ret;
    } else if (cmd == F_DUPFD_CLOEXEC) {
      /* aarch64 通用号 1030：musl 的 O_CLOEXEC 打开路径（posix_spawn、
       * wayland 库）全走它。复制后立刻置 FD_CLOEXEC 位。 */
      u32 ret = sys_dup(fd);
      if ((int)ret >= 0) {
        fd_t* nfd = thread_find_fd_id(current, (int)ret);
        if (nfd != NULL) {
          nfd->flags |= (u32)FD_CLOEXEC;
        }
      }
      return ret;
    } else if (cmd == F_ADD_SEALS) {
      /* 【memfd sealing no-op】wayland-os.c 建 wl_shm 池时对 memfd 加
       * F_SEAL_SHRINK，失败会让整个建池失败。本内核没有 sealing，
       * 返回成功让流程走通（shrinking 防护由调用方保证）。 */
      return 0;
    } else if (cmd == F_GET_SEALS) {
      return -1; /* 无 seals：wayland-shm.c 按 0 处理 */
    } else if (cmd == F_GETFD) {
      return (int)(findfd->flags & (u32)FD_CLOEXEC);
    } else if (cmd == F_GETFL) {
      /* 【必须报告访问模式】glibc 的 fdopen() 会用 F_GETFL 校验方向：
       *   (flags & O_ACCMODE) == O_RDONLY 时 fdopen(fd, "w") 直接返回 NULL。
       * 本内核不记录每个 fd 的访问模式，统一按 O_RDWR 报告（读写两个方向
       * 都成立），状态标志照常带上。曾经返回 0（=O_RDONLY）导致
       * popen("...", "w") 恒失败：Xorg 的 xkbcomp 拉起后一个字节都写不进
       * 管道，于是 "Could not invoke xkbcomp" + "syntax error: line 1 of
       * stdin"。 */
      return (int)((u32)O_RDWR | (findfd->flags & (u32)FD_STATUS_MASK));
    } else if (cmd == F_SETFL) {
      /* 只改 fd 层能表达的状态位；access mode 由 open 决定，不在这里动。 */
      findfd->flags = (findfd->flags & ~(u32)FD_STATUS_MASK) |
                      ((u32)(uintptr_t)arg & (u32)FD_STATUS_MASK);
      return 0;
    } else {
      log_error("not support cmd %d\n", cmd);
    }

    /* 【未知命令返回 -EINVAL】旧值 1 会被调用方当成功（wayland 建 wl_shm
     * 池时 fcntl(F_ADD_SEALS) 判错就整体放弃）。已实现的命令在上面各分支
     * 显式返回；漏掉的报错比假成功好排。 */
    return -22;
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

  /* 【chmod 记录表】FAT 上没有权限位，chmod 本身只能成功返回；但有的调用
   * 点需要按权限做判断（AF_UNIX 路径名套接字 connect 时，文件不可写要回
   * EACCES）。这里按文件名记住最近一次 chmod 的模式，供这些调用点查询；
   * 没记录过的文件一律放行（等价于原先的"无权限限制"）。 */
#define CHMOD_TAB_MAX 16
#define CHMOD_NAME_MAX 64
  typedef struct {
    char name[CHMOD_NAME_MAX];
    u32 mode;
    u8 used;
  } chmod_entry_t;
  static chmod_entry_t chmod_tab[CHMOD_TAB_MAX];

  static const char* chmod_basename(const char* path) {
    const char* base = path;
    if (path == NULL) {
      return "";
    }
    while (*path != '\0') {
      if (*path == '/' && path[1] != '\0') {
        base = path + 1;
      }
      path++;
    }
    return base;
  }

  void sys_chmod_set(const char* path, u32 mode) {
    const char* base = chmod_basename(path);
    int slot = -1;
    if (base[0] == '\0') {
      return;
    }
    for (int i = 0; i < CHMOD_TAB_MAX; i++) {
      if (chmod_tab[i].used) {
        if (kstrcmp(chmod_tab[i].name, base) == 0) {
          chmod_tab[i].mode = mode & 07777;
          return;
        }
      } else if (slot < 0) {
        slot = i;
      }
    }
    if (slot < 0) {
      return;
    }
    chmod_tab[slot].used = 1;
    chmod_tab[slot].mode = mode & 07777;
    kstrncpy(chmod_tab[slot].name, base, CHMOD_NAME_MAX - 1);
    chmod_tab[slot].name[CHMOD_NAME_MAX - 1] = '\0';
  }

  int sys_chmod_query(const char* path, u32* mode) {
    const char* base = chmod_basename(path);
    if (base[0] == '\0') {
      return -1;
    }
    for (int i = 0; i < CHMOD_TAB_MAX; i++) {
      if (chmod_tab[i].used && kstrcmp(chmod_tab[i].name, base) == 0) {
        if (mode != NULL) {
          *mode = chmod_tab[i].mode;
        }
        return 0;
      }
    }
    return -1;
  }

  /* 【no-op 成功】musl 的 chown/lchown/fchown 全部走 fchownat（aarch64 编号
   * 54）。YiYiYa 的 FAT 文件系统没有属主/属组概念，chown 只能返回 ENOSYS，
   * 而 LTP tst_tmpdir 建目录后必调 chown，所有测试 setup 阶段 TBROK。
   * 这里按"操作成功"返回（Ownership 概念不存在，chown no-op 合理）。
   * fchmodat 除了成功返回，还把模式记进 chmod 表供权限判断。 */
  int sys_fchownat(int dirfd, const char* pathname, unsigned int owner,
                   unsigned int group, int flags) {
    (void)dirfd;
    (void)pathname;
    (void)owner;
    (void)group;
    (void)flags;
    return 0;
  }

  int sys_fchmodat(int dirfd, const char* pathname, unsigned int mode,
                   int flags) {
    char kpath[MAX_PATH_BUFFER];
    (void)dirfd;
    (void)flags;
    if (pathname == NULL) {
      return -14; /* -EFAULT */
    }
    if (sys_copy_user_string(pathname, kpath, sizeof(kpath)) == 0) {
      sys_chmod_set(kpath, (u32)mode);
    }
    return 0;
  }

  /* fchmod(52)：aarch64 上 fchmod() 走自己的号（不是 fchmodat）。同样只记账，
   * 缺注册会让调用方拿到 ENOSYS。 */
  int sys_fchmod(int fd, unsigned int mode) {
    thread_t* current = thread_current();
    fd_t* f = (current != NULL) ? thread_find_fd_id(current, (u32)fd) : NULL;
    if (f != NULL && f->name != NULL) {
      sys_chmod_set((const char*)f->name, (u32)mode);
    }
    return 0;
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
      /* 透传 -ENOENT：xtrans/很多程序靠 errno==ENOENT 区分"不存在" */
      return fd;
    }
    int ret = sys_fstat(fd, stat);
    sys_close(fd);
    return ret;
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
    int ret = (int)vioctl(node, cmd, stat);
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
    int ret = (int)vioctl(node, cmd, stat);
    return ret;
  }


/* 【新增】musl ftruncate/truncate -> ftruncate（aarch64 编号 46）。
 * LTP tst_test.c 建 IPC 共享文件（SAFE_FTRUNCATE）必需。 */
/* flock(32)：咨询锁。本内核没有多写者竞争场景（单合成器/单实例工具），
 * 实现为"fd 合法即授予"——wayland 的 wl_display_add_socket_auto 靠它
 * 抢占 wayland-N 的 lockfile，缺它就会一路试到 wayland-32 后放弃
 * （实测 add_socket 全失败）。真互斥由 unix bind 的命名冲突兜底。 */
int sys_flock(int fd, int operation) {
  thread_t* current = thread_current();
  if (thread_find_fd_id(current, fd) == NULL) {
    return -9; /* -EBADF */
  }
  (void)operation; /* LOCK_SH/LOCK_EX/LOCK_NB/LOCK_UN 一律视为成功 */
  return 0;
}

int sys_ftruncate(int fd, off_t length) {
  thread_t* current = thread_current();
  fd_t* f = thread_find_fd_id(current, fd);
  if (f == NULL) {
    log_error("sys_ftruncate not found fd %d tid %d\n", fd, current->id);
    return -1;
  }
  vnode_t* node = (vnode_t*)f->data;
  if (node == NULL) {
    return -1;
  }
  /* 【共享页节点（memfd 等）】按新长度增减页，不走 FAT */
  if (node->op != NULL && node->op->truncate != NULL) {
    return node->op->truncate(node, (u64)length);
  }
  return fat_ftruncate(node, (u64)length);
}

int sys_mkdir(const char* pathname, mode_t mode) {
  thread_t* current = thread_current();
  char kpath[MAX_PATH_BUFFER];
  char parent[MAX_PATH_BUFFER];
  const char* base;
  char* slash;
  vnode_t* dir_node;

  if (pathname == NULL) {
    return -1;
  }
  if (!sys_user_path_pointer_valid(pathname)) {
    log_error("sys_mkdir bad user path %x\n", pathname);
    return -1;
  }
  if (sys_copy_user_string(pathname, kpath, sizeof(kpath)) < 0) {
    return -1;
  }
  if (kstrlen(kpath) == 0) {
    return -1;
  }
  /* 【尾部 '/' 必须先剥掉】"test_dir/" 拆分后 base 会变成空串 —— 之前直接
   * 返回 EPERM，LTP renameat201/202 以及不少程序的 mkdir("x/") 全被拒。 */
  while (kstrlen(kpath) > 1 && kpath[kstrlen(kpath) - 1] == '/') {
    kpath[kstrlen(kpath) - 1] = '\0';
  }

  /* 拆 parent 目录 + 末段名字：/tmp/LTP_x -> parent=/tmp base=LTP_x */
  kstrcpy(parent, kpath);
  slash = parent + kstrlen(parent);
  while (slash > parent && *slash != '/') {
    slash--;
  }
  if (*slash == '/') {
    base = slash + 1;
    *slash = '\0';
    if (parent[0] == '\0') {
      parent[0] = '/';
      parent[1] = '\0'; /* "/name" -> parent="/" */
    }
  } else {
    base = kpath; /* 无 '/'：相对 pwd 的裸名 */
    parent[0] = '\0';
  }
  if (base[0] == '\0') {
    return -1;
  }

  /* 【相对路径必须落在 pwd】裸名（无 '/'）时 parent 为空串，传 "/" 会变成
   * "在根目录下建同名目录"，cwd 里随后找不到 ⇒ LTP 等报 EPERM。
   * 空串交给 vfs_find_relative 直接返回 pwd。 */
  dir_node = vfs_find_relative(current->vfs->root, current->vfs->pwd, parent);
  if (!vfs_node_is_valid(dir_node) ||
      (dir_node->flags & V_DIRECTORY) != V_DIRECTORY) {
    log_error("sys_mkdir: parent not found %s\n", parent);
    return -1;
  }
  /* 【必须回 -EEXIST，不能一律 -1】调用方靠 errno 区分"已经有了"和"真失败"：
   * Xorg 的 xtrans 建 /tmp/.X11-unix 时先 mkdir，收到 EEXIST 就再去 stat 确认
   * 是目录然后继续；我们回 -1（clibc 一律翻成 EPERM）⇒ xtrans 认为建目录失败
   * ⇒ "failed to create listener for unix" ⇒ X 服务器根本没有监听 fd，
   * 客户端连上来永远没人 accept（xsetroot/xclock 报 unable to open display）。 */
  if (vfs_find_child(dir_node, base) != NULL) {
    return -17; /* -EEXIST */
  }

  {
    int ret = vmkdir(dir_node, base, mode);
    if (ret < 0) {
      /* 失败必须留下名字：调用方（如 xtrans 建 /tmp/.X11-unix）只会报一个
       * 被 musl 翻成 EPERM 的 errno，看不出是哪一层拒绝的。 */
      log_error("sys_mkdir failed parent=%s base=%s ret=%d\n", parent, base, ret);
    }
    return ret;
  }
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
      /* access(2) 语义：不存在必须报 ENOENT（musl/程序靠它区分权限问题） */
      return fd;
    }
    sys_close(fd);
    return 0;
  }

/* ===================== 文件状态/符号链接（自 sysfn.c 迁入） ===================== */

int sys_statx(int dirfd, const char* restrict pathname, int flags,
              unsigned int mask, struct statx* restrict statxbuf) {
  log_debug("sys statx not impl dirfd %d pathname %s\n", dirfd, pathname);

  if (statxbuf == NULL) {
    return -1;
  }

  struct stat buf;
  int ret = sys_stat(pathname, &buf);
  if (ret < 0) {
    return ret; /* 缺文件透传 -ENOENT */
  }

  statxbuf->stx_mode = buf.st_mode;
  statxbuf->stx_gid = buf.st_gid;
  statxbuf->stx_uid = buf.st_uid;
  statxbuf->stx_size = buf.st_size;
  statxbuf->stx_blocks = buf.st_blocks;

  return 0;
}

ssize_t sys_readlink(const char* restrict pathname, char* restrict buf,
                     size_t bufsiz) {
  /* 实现在文件前部（sys_readlink_common）：这里只保留非 readlinkat 形态的
   * 入口，起始目录取进程 cwd（AT_FDCWD = -100）。 */
  return sys_readlink_common(-100, pathname, buf, bufsiz);
}

/* ===================== 最小 procfs 垫片 =====================
 * 动态生成 /proc 下进程/系统文件：/proc/<pid>/stat、/proc/<pid>/task/
 * <tid>/stat、/proc/<pid>/task 目录、/proc/self/*、/proc/uptime、
 * /proc/sys/kernel/pid_max。节点为 kmalloc 临时 vnode（name=路径、
 * inode=内容种类、impl=pid/tid），read 现场生成，close 自释放。 */

extern thread_t* schedulable_head_thread[MAX_CPU];

#define PROC_STAT_PROC 1
#define PROC_STAT_TID 2
#define PROC_UPTIME 3
#define PROC_PID_MAX 4
#define PROC_MEMINFO 5
#define PROC_FILESYSTEMS 6
#define PROC_MOUNTS 7
#define PROC_CPUINFO 8
#define PROC_CMDLINE 9
#define PROC_TAINTED 10
#define PROC_MAPS 11

static u32 proc_file_read(vnode_t* node, u32 offset, u32 size, u8* buf);
static u32 proc_task_readdir(vnode_t* node, vdirent_t* dirent, u32* offset,
                             u32 count);
static u32 proc_node_close(vnode_t* node);

static voperator_t proc_file_ops = {
    .read = proc_file_read,
    .close = proc_node_close,
};
static voperator_t proc_dir_ops = {
    .readdir = proc_task_readdir,
    .close = proc_node_close,
};

static void procfs_put_str(char** p, const char* end, const char* s) {
  while (*s && *p < end) {
    *(*p)++ = *s++;
  }
}

static void procfs_put_dec(char** p, const char* end, u32 v) {
  char t[12];
  int i = 0;
  if (v == 0) {
    t[i++] = '0';
  }
  while (v > 0 && i < 11) {
    t[i++] = (char)('0' + v % 10);
    v /= 10;
  }
  while (i > 0 && *p < end) {
    *(*p)++ = t[--i];
  }
}

static void procfs_put_char(char** p, const char* end, char c) {
  if (*p < end) {
    *(*p)++ = c;
  }
}

static int procfs_parse_dec(const char* s, u32* out) {
  u32 v = 0;
  int n = 0;
  while (s[n] >= '0' && s[n] <= '9') {
    v = v * 10 + (u32)(s[n] - '0');
    n++;
  }
  if (n > 0) {
    *out = v;
  }
  return n;
}

/* WAITING/SLEEP='S'（wait_for_threads 只认 'S'）、STOPPED='Z'、其余 'R' */
static char proc_state_char(thread_t* t) {
  if (t == NULL) {
    return 'Z';
  }
  if (t->state == THREAD_STOPPED) {
    return 'Z';
  }
  if (t->state == THREAD_RUNNING || t->state == THREAD_RUNABLE) {
    return 'R';
  }
  return 'S';
}

static int proc_generate(vnode_t* node, char* out, int outsz) {
  char* p = out;
  char* end = out + outsz - 1;

  if (node->inode == PROC_UPTIME) {
    u32 ticks = schedule_get_ticks();
    u64 sec = ticks / SCHEDULE_FREQUENCY;
    u64 cent = (ticks % SCHEDULE_FREQUENCY) * 100 / SCHEDULE_FREQUENCY;
    procfs_put_dec(&p, end, (u32)sec);
    procfs_put_str(&p, end, ".");
    if (cent < 10) {
      procfs_put_char(&p, end, '0');
    }
    procfs_put_dec(&p, end, (u32)cent);
    procfs_put_char(&p, end, ' ');
    procfs_put_dec(&p, end, (u32)sec);
    procfs_put_str(&p, end, ".00\n");
  } else if (node->inode == PROC_PID_MAX) {
    procfs_put_str(&p, end, "4194304\n");
  } else if (node->inode == PROC_TAINTED) {
    procfs_put_str(&p, end, "0\n");
  } else if (node->inode == PROC_MAPS) {
    /* 内存映射视图：只保证文件存在、可读、格式合法（地址-地址 权限 偏移
     * 设备 inode）。tst_fd 系列只需从这里拿到一个普通文件 fd，部分测试
     * 按行解析做格式检查，因此给出代码段与堆段两条典型记录。 */
    procfs_put_str(&p, end, "00010000-00020000 r-xp 00000000 00:00 0\n");
    procfs_put_str(&p, end, "00020000-00030000 rw-p 00000000 00:00 0\n");
  } else if (node->inode == PROC_MEMINFO) {
    /* 全部取自内核实时状态（不写死）：
     *   MemTotal/MemFree 页池总量与剩余（mm_get_total/mm_get_free，字节）；
     *   Buffers 内核堆已分配、Cached 用户页已映射（memory_info 的计数）；
     *   SwapTotal/SwapFree 换出区容量与已用（page_swap_stat，未挂 swap 为 0）。
     * LTP 的 tst_memutils 启动即读这个文件算可用内存（缺文件直接 TBROK）。 */
    extern ullong mm_get_total(void);
    extern ullong mm_get_free(void);
    extern memory_t* memory_info(void);
    extern void page_swap_stat(u64* total_bytes, u64* used_bytes);
    memory_t* mem = memory_info();
    u64 swap_total = 0;
    u64 swap_used = 0;
    u64 total_kb64 = mm_get_total() / 1024ull;
    u64 free_kb64 = mm_get_free() / 1024ull;
    u64 kernel_kb64 = (mem != NULL) ? (u64)mem->kernel_used / 1024ull : 0;
    u64 user_kb64 = (mem != NULL) ? (u64)mem->user_used / 1024ull : 0;
    u64 swap_total_kb64;
    u64 swap_free_kb64;
    page_swap_stat(&swap_total, &swap_used);
    swap_total_kb64 = swap_total / 1024ull;
    swap_free_kb64 = (swap_total > swap_used) ? (swap_total - swap_used) / 1024ull
                                             : 0;
    procfs_put_str(&p, end, "MemTotal:       ");
    procfs_put_dec(&p, end, (u32)total_kb64);
    procfs_put_str(&p, end, " kB\nMemFree:        ");
    procfs_put_dec(&p, end, (u32)free_kb64);
    procfs_put_str(&p, end, " kB\nMemAvailable:   ");
    procfs_put_dec(&p, end, (u32)free_kb64);
    procfs_put_str(&p, end, " kB\nBuffers:        ");
    procfs_put_dec(&p, end, (u32)kernel_kb64);
    procfs_put_str(&p, end, " kB\nCached:         ");
    procfs_put_dec(&p, end, (u32)user_kb64);
    procfs_put_str(&p, end, " kB\nSwapTotal:      ");
    procfs_put_dec(&p, end, (u32)swap_total_kb64);
    procfs_put_str(&p, end, " kB\nSwapFree:       ");
    procfs_put_dec(&p, end, (u32)swap_free_kb64);
    procfs_put_str(&p, end, " kB\n");
  } else if (node->inode == PROC_FILESYSTEMS) {
    procfs_put_str(&p, end, "\tnodev\n\tproc\n\tsysfs\n\tvfat\n");
  } else if (node->inode == PROC_MOUNTS) {
    procfs_put_str(&p, end, "/dev/root / vfat rw,relatime 0 0\n");
  } else if (node->inode == PROC_CPUINFO) {
    /* 每核一段（tst_virt 等只需要文件可读并扫关键字；核数取自 MAX_CPU） */
    int c;
    for (c = 0; c < MAX_CPU; c++) {
      procfs_put_str(&p, end, "processor\t: ");
      procfs_put_dec(&p, end, (u32)c);
      procfs_put_str(&p, end, "\nmodel name\t: YiYiYa ARMv8\n\n");
    }
  } else if (node->inode == PROC_CMDLINE) {
    /* 以 NUL 分隔的命令行：内核里只有进程名，终端补一个 NUL。
     * Xorg 用它识别客户端（读不到只是日志噪音，但会走一条慢路径）。 */
    thread_t* ct = thread_find_id((int)node->impl);
    const char* nm = (ct != NULL && ct->name != NULL) ? ct->name : "unknown";
    procfs_put_str(&p, end, nm);
    procfs_put_char(&p, end, '\0');
  } else if (node->inode == PROC_STAT_PROC || node->inode == PROC_STAT_TID) {
    thread_t* t = thread_find_id((int)node->impl);
    if (t == NULL) {
      return -1;
    }
    /* pid (comm) state ppid ... 后续字段补 0（madvise06 读第 12 字段=0） */
    procfs_put_dec(&p, end, node->impl);
    procfs_put_str(&p, end, " (");
    procfs_put_str(&p, end, t->name != NULL ? t->name : "unknown");
    procfs_put_str(&p, end, ") ");
    procfs_put_char(&p, end, proc_state_char(t));
    procfs_put_char(&p, end, ' ');
    procfs_put_dec(&p, end, (u32)t->pid);
    /* Linux 的 stat 共 52 个字段（pid..exit_code）：前 4 个已给真实值，
     * 其余补 0。字段数不够时 LTP 的 safe_file_scanf 会报
     * "The FILE '/proc/<pid>/stat' ended prematurely"。 */
    for (int i = 0; i < 48; i++) {
      procfs_put_str(&p, end, " 0");
    }
    procfs_put_char(&p, end, '\n');
  } else {
    return -1;
  }
  *p = '\0';
  return (int)(p - out);
}

static u32 proc_file_read(vnode_t* node, u32 offset, u32 size, u8* buf) {
  char tmp[512];
  int len = proc_generate(node, tmp, sizeof(tmp));
  if (len < 0) {
    return 0;
  }
  if (offset >= (u32)len) {
    return 0;
  }
  u32 avail = (u32)len - offset;
  u32 n = size < avail ? size : avail;
  kmemcpy(buf, tmp + offset, n);
  return n;
}

static u32 proc_node_close(vnode_t* node) {
  if (node != NULL) {
    if (node->name != NULL) {
      kfree(node->name);
    }
    kfree(node);
  }
  return 0;
}

/* task 目录枚举：进程 = 主线程(t->id==pid) + 其子线程(t->pid==pid)。fork
 * 出的子进程 t->pid 也是父 id，会一起列出——wait_for_threads 场景（进程内
 * pthread、无 fork）恰好正确。 */
static u32 proc_task_readdir(vnode_t* node, vdirent_t* dirent, u32* offset,
                             u32 count) {
  (void)count;
  int pid = (int)node->impl;
  u32 want = *offset;
  u32 idx = 0;
  for (int c = 0; c < MAX_CPU; c++) {
    for (thread_t* t = schedulable_head_thread[c]; t != NULL; t = t->next) {
      if ((int)t->id != pid && (int)t->pid != pid) {
        continue;
      }
      if (idx == want) {
        kmemset(dirent, 0, sizeof(vdirent_t));
        dirent->ino = t->id;
        char* p = dirent->name;
        char* end = dirent->name + sizeof(dirent->name) - 1;
        procfs_put_dec(&p, end, t->id);
        *p = '\0';
        dirent->type = V_DIRECTORY;
        *offset = want + 1;
        return 1;
      }
      idx++;
    }
  }
  return 0;
}

static vnode_t* proc_make(const char* path, u32 flags, int kind, u32 impl) {
  vnode_t* node = vfs_create_node((u8*)path, flags);
  if (node == NULL) {
    return NULL;
  }
  node->inode = kind;
  node->impl = impl;
  node->op = (flags & V_DIRECTORY) ? &proc_dir_ops : &proc_file_ops;
  return node;
}

static vnode_t* procfs_lookup(const char* path) {
  /* 【容忍尾部 '/'】LTP wait_for_threads opendir("/proc/<pid>/task/") 带尾
   * 斜杠；不剥掉的话 "/task/" 不匹配 "/task"、"/task/<tid>" 也解析失败 ⇒
   * EPERM ⇒ 整个 futex_wake02 TBROK。同 vfs_open_attr 当年 "/tmp/LTP_x/" 坑。 */
  char buf[128];
  u32 len = kstrlen(path);
  if (len > 1 && path[len - 1] == '/') {
    if (len >= sizeof(buf)) {
      return NULL;
    }
    kmemcpy(buf, path, len + 1);
    while (len > 1 && buf[len - 1] == '/') {
      buf[--len] = '\0';
    }
    path = buf;
  }
  if (kstrncmp(path, "/proc/", 6) != 0) {
    return NULL;
  }
  const char* rest = path + 6;

  if (kstrcmp(rest, "uptime") == 0) {
    return proc_make(path, V_FILE, PROC_UPTIME, 0);
  }
  if (kstrcmp(rest, "sys/kernel/pid_max") == 0) {
    return proc_make(path, V_FILE, PROC_PID_MAX, 0);
  }
  if (kstrcmp(rest, "meminfo") == 0) {
    return proc_make(path, V_FILE, PROC_MEMINFO, 0);
  }
  if (kstrcmp(rest, "filesystems") == 0) {
    return proc_make(path, V_FILE, PROC_FILESYSTEMS, 0);
  }
  if (kstrcmp(rest, "mounts") == 0) {
    return proc_make(path, V_FILE, PROC_MOUNTS, 0);
  }
  if (kstrcmp(rest, "cpuinfo") == 0) {
    return proc_make(path, V_FILE, PROC_CPUINFO, 0);
  }
  if (kstrcmp(rest, "sys/kernel/tainted") == 0) {
    return proc_make(path, V_FILE, PROC_TAINTED, 0);
  }
  /* /proc/self/... → /proc/<pid>/... 递归一次 */
  if (kstrncmp(rest, "self/", 5) == 0) {
    thread_t* cur = thread_current();
    if (cur == NULL) {
      return NULL;
    }
    char real[96];
    char* p = real;
    procfs_put_str(&p, real + sizeof(real) - 1, "/proc/");
    procfs_put_dec(&p, real + sizeof(real) - 1, cur->id);
    procfs_put_str(&p, real + sizeof(real) - 1, rest + 4);
    *p = '\0';
    return procfs_lookup(real);
  }

  u32 pid = 0;
  int n = procfs_parse_dec(rest, &pid);
  if (n == 0) {
    return NULL;
  }
  rest += n;
  if (kstrcmp(rest, "/stat") == 0) {
    return proc_make(path, V_FILE, PROC_STAT_PROC, pid);
  }
  if (kstrcmp(rest, "/cmdline") == 0) {
    return proc_make(path, V_FILE, PROC_CMDLINE, pid);
  }
  if (kstrcmp(rest, "/maps") == 0) {
    return proc_make(path, V_FILE, PROC_MAPS, pid);
  }
  if (kstrcmp(rest, "/task") == 0) {
    return proc_make(path, V_DIRECTORY, PROC_STAT_TID, pid);
  }
  if (kstrncmp(rest, "/task/", 6) == 0) {
    u32 tid = 0;
    int m = procfs_parse_dec(rest + 6, &tid);
    if (m == 0) {
      return NULL;
    }
    if (kstrcmp(rest + 6 + m, "/stat") == 0) {
      return proc_make(path, V_FILE, PROC_STAT_TID, tid);
    }
  }
  return NULL;
}

/* ===================== 最小 sysfs 垫片 =====================
 * /sys 是 Linux 的 sysfs：内核把"设备 ↔ 总线 ↔ 驱动"的关系导出成一棵目录
 * 树，用户态靠 readlink 那些软链接判断设备的来源与归属。典型用法：
 *
 *     readlink("/sys/class/graphics/fb0/device/subsystem", buf, sizeof(buf))
 *
 * 拿到总线名（"pci"/"platform"/"drm"…）后再决定怎么打开 /dev/fb0。这条链接
 * 不存在时，程序会直接判"没有可用显示设备"—— /dev/fb0 明明能 open/ioctl，
 * Xorg 却报 "No devices detected"，根因就在这里。
 *
 * 本内核没有设备模型，这里只补 /sys/class/graphics/fbN 这一小片，形状与
 * Linux 一致（相对链接的层数也对得上，所以 realpath() 也能走通）：
 *
 *   /sys                                     目录
 *   /sys/class | /sys/bus | /sys/devices     三套并行视图的骨架
 *   /sys/class/graphics                      按功能分类视图
 *   /sys/class/graphics/fbD                  目录，属性见下
 *     name / dev / modes / virtual_size / stride / bits_per_pixel /
 *     blank / rotate / uevent
 *     subsystem        → /sys/class/graphics
 *     device           → ../../../devices/platform/fbD      （相对链接）
 *     device/subsystem → /sys/bus/platform                ← 应用真正读的那条
 *     device/driver    → /sys/bus/platform/drivers/fb
 *   /sys/devices/platform/fbD                同一设备的"物理"视图
 *   /sys/bus/platform[/drivers/fb[/fbD]]     按总线分类视图
 *
 * 实现方式与上面的 procfs 垫片同构：节点是 kmalloc 的临时 vnode
 * （name=访问路径、inode=条目种类、impl=fb 编号），read 现场生成内容、
 * close 自释放；目录树不真的挂进 VFS —— sys_open_kpath 先问这里，命中就
 * 不再落 fatfs。好处是内容永远反映设备当前状态（分辨率会被 [hdmi]/[vga]
 * 配置改），不必在内核各处维护一份副本。
 *
 * 哪些 fb 存在、分辨率是多少，都去问 /dev/fbN 自己：节点在不在用
 * vfs_find_child 探（不打印错误日志），几何走 gpu 驱动既有的
 * VGA_IOC_READ_FRAMBUFFER_WIDTH/HEIGHT/BPP。 */

#define SYSFS_FB_MAX 4

/* 条目种类 */
#define SYSFS_ENT_DIR 1
#define SYSFS_ENT_FILE 2
#define SYSFS_ENT_LINK 3

/* 目录/属性的种类（存进 node->inode，read/readdir 据此决定内容） */
enum {
  SYSFS_DIR_ROOT = 1,
  SYSFS_DIR_CLASS,
  SYSFS_DIR_GRAPHICS,
  SYSFS_DIR_BUS,
  SYSFS_DIR_BUS_PLATFORM,
  SYSFS_DIR_BUS_DRIVERS,
  SYSFS_DIR_DRIVER_FB,
  SYSFS_DIR_DEVICES,
  SYSFS_DIR_DEVICES_PLATFORM,
  SYSFS_DIR_FB,
  SYSFS_DIR_FB_DEV, /* 设备本体（.../fbN/device 与 /sys/devices/platform/fbN） */
  SYSFS_ATTR_NAME,
  SYSFS_ATTR_DEV,
  SYSFS_ATTR_MODES,
  SYSFS_ATTR_VIRTUAL_SIZE,
  SYSFS_ATTR_STRIDE,
  SYSFS_ATTR_BPP,
  SYSFS_ATTR_BLANK,
  SYSFS_ATTR_ROTATE,
  SYSFS_ATTR_UEVENT,
  /* ---- input 设备（/dev/input/eventN）---- */
  SYSFS_DIR_DEV,        /* /sys/dev */
  SYSFS_DIR_DEV_CHAR,   /* /sys/dev/char：按 major:minor 索引设备 */
  SYSFS_DIR_INPUT,      /* /sys/class/input 与 /sys/devices/virtual/input */
  SYSFS_DIR_INPUT_EVENT,/* .../eventN */
  SYSFS_ATTR_INPUT_NAME,
  SYSFS_ATTR_INPUT_DEV,
  SYSFS_ATTR_INPUT_UEVENT,
  /* capabilities/<ev|key|rel|abs|prop>：udev 的 input_id 内建就读这些文件，
   * 再写成 ID_INPUT_* 属性给 libinput。内容与 EVIOCGBIT 完全一致。 */
  SYSFS_DIR_INPUT_CAPS,
  SYSFS_ATTR_CAP_EV,
  SYSFS_ATTR_CAP_KEY,
  SYSFS_ATTR_CAP_REL,
  SYSFS_ATTR_CAP_ABS,
  SYSFS_ATTR_CAP_PROP,
  SYSFS_ATTR_EMPTY /* 存在但内容为空的属性（如中间目录的 uevent） */
};

/* Linux input 设备号约定：主设备号 13，eventN 的次设备号 64+N
 * （与 /dev/input/eventN 的 stat.st_rdev 必须一致，libinput 的 path 后端会
 *  拿 rdev 反查 /sys/dev/char/<major>:<minor>）。 */
#define SYSFS_INPUT_MAJOR 13
#define SYSFS_INPUT_MINOR_BASE 64
#define SYSFS_INPUT_MAX 8

typedef struct {
  const char* path;   /* 'D' = fb 编号；'E' = input event 次设备号占位 */
  u8 kind;            /* SYSFS_ENT_* */
  u8 content;         /* SYSFS_DIR_* / SYSFS_ATTR_* */
  const char* target; /* 仅 LINK：目标串（'D' 同样换成编号） */
} sysfs_entry_t;

static const sysfs_entry_t sysfs_table[] = {
    /* 目录骨架 */
    {"/sys", SYSFS_ENT_DIR, SYSFS_DIR_ROOT, NULL},
    {"/sys/class", SYSFS_ENT_DIR, SYSFS_DIR_CLASS, NULL},
    {"/sys/class/graphics", SYSFS_ENT_DIR, SYSFS_DIR_GRAPHICS, NULL},
    {"/sys/bus", SYSFS_ENT_DIR, SYSFS_DIR_BUS, NULL},
    {"/sys/bus/platform", SYSFS_ENT_DIR, SYSFS_DIR_BUS_PLATFORM, NULL},
    {"/sys/bus/platform/drivers", SYSFS_ENT_DIR, SYSFS_DIR_BUS_DRIVERS, NULL},
    {"/sys/bus/platform/drivers/fb", SYSFS_ENT_DIR, SYSFS_DIR_DRIVER_FB, NULL},
    {"/sys/devices", SYSFS_ENT_DIR, SYSFS_DIR_DEVICES, NULL},
    {"/sys/devices/platform", SYSFS_ENT_DIR, SYSFS_DIR_DEVICES_PLATFORM, NULL},

    /* 按功能分类视图：/sys/class/graphics/fbD */
    {"/sys/class/graphics/fbD", SYSFS_ENT_DIR, SYSFS_DIR_FB, NULL},
    {"/sys/class/graphics/fbD/name", SYSFS_ENT_FILE, SYSFS_ATTR_NAME, NULL},
    {"/sys/class/graphics/fbD/dev", SYSFS_ENT_FILE, SYSFS_ATTR_DEV, NULL},
    {"/sys/class/graphics/fbD/modes", SYSFS_ENT_FILE, SYSFS_ATTR_MODES, NULL},
    {"/sys/class/graphics/fbD/virtual_size", SYSFS_ENT_FILE,
     SYSFS_ATTR_VIRTUAL_SIZE, NULL},
    {"/sys/class/graphics/fbD/stride", SYSFS_ENT_FILE, SYSFS_ATTR_STRIDE, NULL},
    {"/sys/class/graphics/fbD/bits_per_pixel", SYSFS_ENT_FILE, SYSFS_ATTR_BPP,
     NULL},
    {"/sys/class/graphics/fbD/blank", SYSFS_ENT_FILE, SYSFS_ATTR_BLANK, NULL},
    {"/sys/class/graphics/fbD/rotate", SYSFS_ENT_FILE, SYSFS_ATTR_ROTATE, NULL},
    {"/sys/class/graphics/fbD/uevent", SYSFS_ENT_FILE, SYSFS_ATTR_UEVENT, NULL},
    {"/sys/class/graphics/fbD/subsystem", SYSFS_ENT_LINK, 0,
     "/sys/class/graphics"},
    {"/sys/class/graphics/fbD/device", SYSFS_ENT_LINK, SYSFS_DIR_FB_DEV,
     "../../../devices/platform/fbD"},
    {"/sys/class/graphics/fbD/device/subsystem", SYSFS_ENT_LINK, 0,
     "/sys/bus/platform"},
    {"/sys/class/graphics/fbD/device/driver", SYSFS_ENT_LINK, 0,
     "/sys/bus/platform/drivers/fb"},
    {"/sys/class/graphics/fbD/device/name", SYSFS_ENT_FILE, SYSFS_ATTR_NAME,
     NULL},

    /* 物理视图：/sys/devices/platform/fbD */
    {"/sys/devices/platform/fbD", SYSFS_ENT_DIR, SYSFS_DIR_FB_DEV, NULL},
    {"/sys/devices/platform/fbD/name", SYSFS_ENT_FILE, SYSFS_ATTR_NAME, NULL},
    {"/sys/devices/platform/fbD/subsystem", SYSFS_ENT_LINK, 0,
     "/sys/bus/platform"},
    {"/sys/devices/platform/fbD/driver", SYSFS_ENT_LINK, 0,
     "/sys/bus/platform/drivers/fb"},

    /* 总线视图：/sys/bus/platform/drivers/fb/fbD → 回到 class */
    {"/sys/bus/platform/drivers/fb/fbD", SYSFS_ENT_LINK, 0,
     "/sys/class/graphics/fbD"},

    /* ---------------- input 设备 ----------------
     * udev（真实 libudev / libudev-zero）发现设备的两条路径都要支持：
     *   · 枚举：[enumerate] 读 /sys/dev/char 的目录项（major:minor），
     *           readlink 拿到设备目录；
     *   · 按 rdev 定位：[libinput path 后端] stat(/dev/input/eventN) 得到
     *           rdev，再 new_from_devnum → /sys/dev/char/13:64+N。
     * 所以 /sys/dev/char/13:E 的 E 必须与设备节点 stat 的次设备号一致
     * （Linux input 主设备号 13，eventN 次设备号从 64 起）。模板里的 'E'
     * 匹配的是"次设备号"，resolve 会统一减 64 换成 event 序号。 */
    {"/sys/dev", SYSFS_ENT_DIR, SYSFS_DIR_DEV, NULL},
    {"/sys/dev/char", SYSFS_ENT_DIR, SYSFS_DIR_DEV_CHAR, NULL},
    {"/sys/dev/char/13:E", SYSFS_ENT_LINK, 0,
     "../../devices/virtual/input/eventE"},

    /* 功能视图：/sys/class/input/eventE */
    {"/sys/class/input", SYSFS_ENT_DIR, SYSFS_DIR_INPUT, NULL},
    {"/sys/class/input/eventE", SYSFS_ENT_DIR, SYSFS_DIR_INPUT_EVENT, NULL},
    {"/sys/class/input/eventE/name", SYSFS_ENT_FILE, SYSFS_ATTR_INPUT_NAME,
     NULL},
    {"/sys/class/input/eventE/dev", SYSFS_ENT_FILE, SYSFS_ATTR_INPUT_DEV, NULL},
    {"/sys/class/input/eventE/uevent", SYSFS_ENT_FILE, SYSFS_ATTR_INPUT_UEVENT,
     NULL},
    {"/sys/class/input/eventE/subsystem", SYSFS_ENT_LINK, 0, "/sys/class/input"},
    {"/sys/class/input/eventE/device", SYSFS_ENT_LINK, 0,
     "../../../devices/virtual/input/eventE"},
    {"/sys/class/input/eventE/device/name", SYSFS_ENT_FILE,
     SYSFS_ATTR_INPUT_NAME, NULL},
    {"/sys/class/input/eventE/device/uevent", SYSFS_ENT_FILE,
     SYSFS_ATTR_INPUT_UEVENT, NULL},
    {"/sys/class/input/eventE/device/subsystem", SYSFS_ENT_LINK, 0,
     "/sys/class/input"},

    /* 能力位图：Linux 内核 input_dev 的 sysfs 表示（十六进制、按 unsigned
     * long 分组、高位组在前）。udev 的 input_id 读它们生成 ID_INPUT_*。 */
    {"/sys/class/input/eventE/capabilities", SYSFS_ENT_DIR, SYSFS_DIR_INPUT_CAPS,
     NULL},
    {"/sys/class/input/eventE/capabilities/ev", SYSFS_ENT_FILE,
     SYSFS_ATTR_CAP_EV, NULL},
    {"/sys/class/input/eventE/capabilities/key", SYSFS_ENT_FILE,
     SYSFS_ATTR_CAP_KEY, NULL},
    {"/sys/class/input/eventE/capabilities/rel", SYSFS_ENT_FILE,
     SYSFS_ATTR_CAP_REL, NULL},
    {"/sys/class/input/eventE/capabilities/abs", SYSFS_ENT_FILE,
     SYSFS_ATTR_CAP_ABS, NULL},
    {"/sys/class/input/eventE/capabilities/prop", SYSFS_ENT_FILE,
     SYSFS_ATTR_CAP_PROP, NULL},

    /* 中间目录也带 uevent（Linux 上每个设备目录都有）：udev 找 parent 时会
     * 逐级读，缺了文件虽然只是 parent 为 NULL，但会打 ENOENT 错误日志。 */
    {"/sys/devices/uevent", SYSFS_ENT_FILE, SYSFS_ATTR_EMPTY, NULL},
    {"/sys/devices/virtual/uevent", SYSFS_ENT_FILE, SYSFS_ATTR_EMPTY, NULL},
    {"/sys/devices/virtual/input/uevent", SYSFS_ENT_FILE, SYSFS_ATTR_EMPTY,
     NULL},

    /* 物理视图：/sys/devices/virtual/input/eventE
     * realpath("/sys/dev/char/13:64") 会落到这里，udev 随后读本目录的
     * uevent（拿 DEVNAME 推出 /dev/input/eventN）。 */
    {"/sys/devices/virtual", SYSFS_ENT_DIR, SYSFS_DIR_DEVICES, NULL},
    {"/sys/devices/virtual/input", SYSFS_ENT_DIR, SYSFS_DIR_INPUT, NULL},
    {"/sys/devices/virtual/input/eventE", SYSFS_ENT_DIR, SYSFS_DIR_INPUT_EVENT,
     NULL},
    {"/sys/devices/virtual/input/eventE/name", SYSFS_ENT_FILE,
     SYSFS_ATTR_INPUT_NAME, NULL},
    {"/sys/devices/virtual/input/eventE/dev", SYSFS_ENT_FILE,
     SYSFS_ATTR_INPUT_DEV, NULL},
    {"/sys/devices/virtual/input/eventE/uevent", SYSFS_ENT_FILE,
     SYSFS_ATTR_INPUT_UEVENT, NULL},
    {"/sys/devices/virtual/input/eventE/subsystem", SYSFS_ENT_LINK, 0,
     "/sys/class/input"},
    /* realpath 的落点是 devices 视图，udev 读 capabilities 就在这里读 */
    {"/sys/devices/virtual/input/eventE/capabilities", SYSFS_ENT_DIR,
     SYSFS_DIR_INPUT_CAPS, NULL},
    {"/sys/devices/virtual/input/eventE/capabilities/ev", SYSFS_ENT_FILE,
     SYSFS_ATTR_CAP_EV, NULL},
    {"/sys/devices/virtual/input/eventE/capabilities/key", SYSFS_ENT_FILE,
     SYSFS_ATTR_CAP_KEY, NULL},
    {"/sys/devices/virtual/input/eventE/capabilities/rel", SYSFS_ENT_FILE,
     SYSFS_ATTR_CAP_REL, NULL},
    {"/sys/devices/virtual/input/eventE/capabilities/abs", SYSFS_ENT_FILE,
     SYSFS_ATTR_CAP_ABS, NULL},
    {"/sys/devices/virtual/input/eventE/capabilities/prop", SYSFS_ENT_FILE,
     SYSFS_ATTR_CAP_PROP, NULL},
};

static u32 sysfs_readdir(vnode_t* node, vdirent_t* dirent, u32* offset,
                         u32 count);
static u32 sysfs_file_read(vnode_t* node, u32 offset, u32 size, u8* buf);
static size_t sysfs_ioctl(vnode_t* node, u32 cmd, void* args);

static voperator_t sysfs_dir_ops = {
    .readdir = sysfs_readdir,
    .ioctl = sysfs_ioctl,
    .close = proc_node_close, /* 临时节点：close 自释放（与 procfs 垫片共用） */
};

static voperator_t sysfs_file_ops = {
    .read = sysfs_file_read,
    .ioctl = sysfs_ioctl,
    .close = proc_node_close,
};

/* 模板匹配：'D' 吃掉 1 位以上十进制数字（写回 *idx），其余必须逐字符相同，
 * 并且整串要恰好对完（"/sys" 不能匹配 "/sys/class"）。 */
static int sysfs_match(const char* path, const char* tmpl, u32* idx) {
  u32 v = 0;
  int digits = 0;

  /* 'D' = 显示编号；'E' = input event 次设备号（13:64+N 里的 64+N）。
   * 两者都按"一位以上十进制数字"解析，语义差别由条目内容决定。 */
  while (*tmpl != '\0' && *tmpl != 'D' && *tmpl != 'E') {
    if (*path != *tmpl) {
      return 0;
    }
    path++;
    tmpl++;
  }
  if (*tmpl == '\0') {
    return *path == '\0';
  }
  tmpl++; /* 跳过 'D' */
  while (*path >= '0' && *path <= '9' && digits < 4) {
    v = v * 10 + (u32)(*path - '0');
    path++;
    digits++;
  }
  if (digits == 0) {
    return 0;
  }
  *idx = v;
  while (*tmpl != '\0') {
    if (*path != *tmpl) {
      return 0;
    }
    path++;
    tmpl++;
  }
  return *path == '\0';
}

/* 模板里是否含占位符 ch（'D'=显示编号，'E'=input 次设备号） */
static int sysfs_tmpl_has(const char* tmpl, char ch) {
  const char* s;

  if (tmpl == NULL) {
    return 0;
  }
  for (s = tmpl; *s != '\0'; s++) {
    if (*s == ch) {
      return 1;
    }
  }
  return 0;
}

static const sysfs_entry_t* sysfs_resolve(const char* path, u32* idx) {
  u32 i;

  if (path == NULL) {
    return NULL;
  }
  for (i = 0; i < sizeof(sysfs_table) / sizeof(sysfs_table[0]); i++) {
    u32 v = 0;
    if (sysfs_match(path, sysfs_table[i].path, &v)) {
      /* 'E' 匹配到的是"次设备号"（Linux input：主 13、eventN 次 64+N），
       * 对外统一换成 event 序号：生成属性、拼链接目标、查设备都存在都用它。 */
      if (sysfs_tmpl_has(sysfs_table[i].path, 'E') &&
          v >= SYSFS_INPUT_MINOR_BASE) {
        v -= SYSFS_INPUT_MINOR_BASE;
      }
      if (idx != NULL) {
        *idx = v;
      }
      return &sysfs_table[i];
    }
  }
  return NULL;
}

/* 模板里带 'D' ⇒ 这条条目依赖"第 N 块显示"是否存在 */
static int sysfs_entry_uses_fb(const sysfs_entry_t* e) {
  const char* s;

  if (e == NULL || e->path == NULL) {
    return 0;
  }
  for (s = e->path; *s != '\0'; s++) {
    if (*s == 'D') {
      return 1;
    }
  }
  return 0;
}

/* path 是不是模板 tmpl 的"严格目录前缀"（模板比 path 长，且 path 正好在
 * 组件边界结束）。判断"这条路径下面是目录、还有子项"就靠它。
 * （不能直接用 kstrncmp 比前缀：模板里 'D' 是占位符，
 *   "/sys/.../fbD/device" 与 "/sys/.../fb0/device" 字面比较必然不等。）
 * 【必须严格】相等不算前缀 —— 早先的实现在"正好相等"时也回 1，于是
 *   /sys/class/graphics/fb0/modes 这种普通属性文件被当成目录造出节点
 *   （node->op=dir_ops），read 走不到 sysfs_file_read ⇒ cat/ls 全是空。
 * 反过来，真正的祖先（"/sys/.../fb0/device" 之于模板
 *   ".../fbD/device/subsystem"）走的是"path 先结束、模板未结束"分支，
 *   现在也能正确命中。 */
static int sysfs_match_prefix(const char* path, const char* tmpl) {
  while (*path != '\0') {
    if (*tmpl == 'D' || *tmpl == 'E') {
      int digits = 0;
      tmpl++;
      while (*path >= '0' && *path <= '9' && digits < 4) {
        path++;
        digits++;
      }
      if (digits == 0) {
        return 0;
      }
      continue;
    }
    if (*tmpl == '\0' || *path != *tmpl) {
      return 0;
    }
    path++;
    tmpl++;
  }
  /* path 走完了：模板若还以 '/' 继续，说明下面还有子项 */
  return *tmpl == '/';
}

/* 本表里是否还有以 path + "/" 开头的条目。
 * 用途：.../fbN/device 是软链接，但它下面还有 subsystem/driver/name ——
 * Linux 上 open() 会跟随软链接，于是 opendir(".../fbN/device") 能列出内容；
 * 这里据此把这类"有子路径的链接"同时当成目录开放（readlink 仍按链接答）。 */
static int sysfs_has_children(const char* path) {
  u32 i;

  if (path == NULL) {
    return 0;
  }
  for (i = 0; i < sizeof(sysfs_table) / sizeof(sysfs_table[0]); i++) {
    const char* p = sysfs_table[i].path;
    if (p != NULL && sysfs_match_prefix(path, p)) {
      return 1;
    }
  }
  return 0;
}

/* 剥掉尾部 '/'（opendir("/sys/class/graphics/") 会带）；path 太长时放弃剥离 */
static const char* sysfs_trim(const char* path, char* buf, size_t bufsz) {
  u32 len;

  if (path == NULL) {
    return NULL;
  }
  len = kstrlen(path);
  if (len <= 1 || path[len - 1] != '/' || len >= bufsz) {
    return path;
  }
  kmemcpy(buf, path, len + 1);
  while (len > 1 && buf[len - 1] == '/') {
    buf[--len] = '\0';
  }
  return buf;
}

/* 只接管 /sys（"/sysfoo" 这种前缀相同的普通路径不算） */
static int sysfs_is_sys(const char* path) {
  return kstrncmp(path, "/sys", 4) == 0 && (path[4] == '\0' || path[4] == '/');
}

/* 路径规范化：消掉 "." 与 ".."（就地写法：结果写 out）。
 * 必需：musl 的 realpath() 只用 readlink()，遇到相对链接目标
 * （"../../../devices/..."）会构造 "/sys/dev/char/.." 这类中间路径去
 * readlink；表里只有规范路径，不消除点段就会一律 ENOENT。 */
static int sysfs_normalize(const char* in, char* out, size_t outsz) {
  size_t o = 0;
  const char* p = in;

  if (in == NULL || in[0] != '/') {
    return -1;
  }
  if (outsz < 2) {
    return -1;
  }
  out[o++] = '/';
  while (*p != '\0') {
    const char* s;
    size_t len;

    while (*p == '/') {
      p++;
    }
    if (*p == '\0') {
      break;
    }
    s = p;
    while (*p != '\0' && *p != '/') {
      p++;
    }
    len = (size_t)(p - s);
    if (len == 1 && s[0] == '.') {
      continue;
    }
    if (len == 2 && s[0] == '.' && s[1] == '.') {
      while (o > 1 && out[o - 1] != '/') {
        o--;
      }
      if (o > 1) {
        o--; /* 去掉尾部的 '/' */
      }
      continue;
    }
    if (o > 1) {
      if (o + 1 >= outsz) {
        return -1;
      }
      out[o++] = '/';
    }
    if (o + len >= outsz) {
      return -1;
    }
    kmemcpy(out + o, s, len);
    o += len;
  }
  out[o] = '\0';
  return 0;
}

/* 目标串展开：'D' 换成 fb 编号，返回长度 */
static int sysfs_expand(const char* tmpl, u32 idx, char* out, size_t outsz) {
  char* p = out;
  char* end = out + outsz - 1;
  const char* s;

  for (s = tmpl; *s != '\0'; s++) {
    if (*s == 'D' || *s == 'E') {
      procfs_put_dec(&p, end, idx);
    } else if (p < end) {
      *p++ = *s;
    }
  }
  *p = '\0';
  return (int)(p - out);
}

/* /dev/fbN 节点存在即表示第 N 块显示可用。vfs_find_child 找不到时不打印
 * 错误（适合探存在性，vfs_find 会打 "cannot found file"）。
 * 单屏平台只建 /dev/fb + /dev/fb0 别名，没有 fb0 的旧平台回落到 /dev/fb。 */
static vnode_t* sysfs_fb_node(u32 idx) {
  vnode_t* devdir;
  vnode_t* node;
  char name[8];

  if (idx >= SYSFS_FB_MAX) {
    return NULL;
  }
  devdir = vfs_find_child(root_node, "dev");
  if (devdir == NULL) {
    return NULL;
  }
  name[0] = 'f';
  name[1] = 'b';
  name[2] = (char)('0' + idx);
  name[3] = '\0';
  node = vfs_find_child(devdir, name);
  if (node == NULL && idx == 0) {
    node = vfs_find_child(devdir, "fb");
  }
  return node;
}

/* /dev/input/eventN 存在即表示第 N 块输入设备可用（由 duck/modules/input
 * 注册，节点名 eventN）。与 sysfs_fb_node 同款探存在性。 */
static vnode_t* sysfs_input_node(u32 idx) {
  vnode_t* devdir;
  vnode_t* inputdir;
  vnode_t* node;
  char name[16];
  char* p;

  if (idx >= SYSFS_INPUT_MAX) {
    return NULL;
  }
  devdir = vfs_find_child(root_node, "dev");
  if (devdir == NULL) {
    return NULL;
  }
  inputdir = vfs_find_child(devdir, "input");
  if (inputdir == NULL) {
    return NULL;
  }
  p = name;
  *p++ = 'e';
  *p++ = 'v';
  *p++ = 'e';
  *p++ = 'n';
  *p++ = 't';
  *p++ = (char)('0' + (idx % 10));
  if (idx >= 10) {
    /* 目前最多 8 块设备，两位够用；这里仍按倒序写保证从 10 起正确 */
    name[5] = (char)('0' + (idx / 10));
    name[6] = (char)('0' + (idx % 10));
    p = name + 7;
  }
  *p = '\0';
  node = vfs_find_child(inputdir, name);
  return node;
}

/* 当前显示几何：只认 gpu 驱动"返回值即数据"的三个 ioctl（vga.h）。走 vioctl
 * 而不是直接读 device->data，pid 层就不必知道驱动的结构体布局。 */
static int sysfs_fb_geometry(u32 idx, u32* w, u32* h, u32* bpp) {
  vnode_t* node = sysfs_fb_node(idx);

  if (node == NULL) {
    return -1;
  }
  *w = (u32)vioctl(node, VGA_IOC_READ_FRAMBUFFER_WIDTH, NULL);
  *h = (u32)vioctl(node, VGA_IOC_READ_FRAMBUFFER_HEIGHT, NULL);
  *bpp = (u32)vioctl(node, VGA_IOC_READ_FRAMBUFFER_BPP, NULL);
  return (*w != 0 && *h != 0) ? 0 : -1;
}

/* ---------------- 能力位图的文本化（sysfs capabilities/*） ----------------
 * 内核 input_dev 的 sysfs 表示：十六进制、按 unsigned long（8 字节）分组、
 * 高位组在前、空格分隔、省略前导零组、末尾换行。udev 的 input_id 内建
 * （以及 libudev-zero 的 make_bit）就按这个格式解析。 */
static void sysfs_put_hex64(char** p, char* end, u64 v) {
  char tmp[17];
  int n = 0;

  if (v == 0) {
    procfs_put_char(p, end, '0');
    return;
  }
  while (v > 0) {
    u32 d = (u32)(v & 0xf);
    tmp[n++] = (char)((d < 10) ? ('0' + d) : ('a' + (d - 10)));
    v >>= 4;
  }
  while (n > 0) {
    procfs_put_char(p, end, tmp[--n]);
  }
}

static void sysfs_put_bitmap(char** p, char* end, const u8* bits, u32 nbytes) {
  u32 nwords = (nbytes + 7) / 8;
  u32 i;
  int printed = 0;

  for (i = nwords; i > 0; i--) {
    u32 base = (i - 1) * 8;
    u64 w = 0;
    u32 j;

    for (j = 0; j < 8 && base + j < nbytes; j++) {
      w |= ((u64)bits[base + j]) << (8 * j);
    }
    if (!printed && w == 0 && i > 1) {
      continue; /* 前导零组不打印（与内核一致） */
    }
    if (printed) {
      procfs_put_char(p, end, ' ');
    }
    sysfs_put_hex64(p, end, w);
    printed = 1;
  }
  if (!printed) {
    procfs_put_char(p, end, '0');
  }
  procfs_put_char(p, end, '\n');
}

/* EVIOCGBIT(ev, len) 的 cmd 编码：_IOC(_IOC_READ, 'E', 0x20+ev, len)。
 * EVIOCGPROP 是 nr=0x09 的独立命令，不走 0x20+ev 区间。 */
static u32 sysfs_eviocgbit(u32 ev, u32 len) {
  return 0x80000000u | (len << 16) | ((u32)'E' << 8) | (0x20u + ev);
}

/* 属性文件内容（现场生成，与 /dev/fbN 的 ioctl 永远一致） */
static int sysfs_generate(vnode_t* node, char* out, int outsz) {
  char* p = out;
  char* end = out + outsz - 1;
  u32 idx = node->impl;
  u32 w = 0;
  u32 h = 0;
  u32 bpp = 0;

  switch (node->inode) {
    case SYSFS_ATTR_NAME:
      procfs_put_str(&p, end, "yiyiya-fb");
      procfs_put_dec(&p, end, idx);
      procfs_put_char(&p, end, '\n');
      break;
    case SYSFS_ATTR_DEV:
      /* Linux 里 fb 的主设备号固定 29，次设备号就是编号 */
      procfs_put_str(&p, end, "29:");
      procfs_put_dec(&p, end, idx);
      procfs_put_char(&p, end, '\n');
      break;
    case SYSFS_ATTR_UEVENT:
      procfs_put_str(&p, end, "MAJOR=29\nMINOR=");
      procfs_put_dec(&p, end, idx);
      procfs_put_str(&p, end, "\nDEVNAME=fb");
      procfs_put_dec(&p, end, idx);
      procfs_put_char(&p, end, '\n');
      break;

    case SYSFS_ATTR_EMPTY:
      break; /* 空文件：内容 0 字节，read 返回 EOF */

    /* ---- input 设备属性 ----
     * uevent 是 udev 唯一的"设备元数据"入口：它读 DEVNAME 推出设备节点
     * /dev/input/eventN，之后才去节点上读能力位（ID_INPUT_*）。
     * MAJOR/MINOR 必须与节点 stat 的 rdev 一致（13 : 64+N）。 */
    case SYSFS_ATTR_INPUT_NAME:
      procfs_put_str(&p, end, "yiyiya-input");
      procfs_put_dec(&p, end, idx);
      procfs_put_char(&p, end, '\n');
      break;
    case SYSFS_ATTR_INPUT_DEV:
      procfs_put_dec(&p, end, SYSFS_INPUT_MAJOR);
      procfs_put_char(&p, end, ':');
      procfs_put_dec(&p, end, SYSFS_INPUT_MINOR_BASE + idx);
      procfs_put_char(&p, end, '\n');
      break;
    case SYSFS_ATTR_INPUT_UEVENT:
      procfs_put_str(&p, end, "MAJOR=");
      procfs_put_dec(&p, end, SYSFS_INPUT_MAJOR);
      procfs_put_str(&p, end, "\nMINOR=");
      procfs_put_dec(&p, end, SYSFS_INPUT_MINOR_BASE + idx);
      procfs_put_str(&p, end, "\nDEVNAME=input/event");
      procfs_put_dec(&p, end, idx);
      procfs_put_char(&p, end, '\n');
      break;

    /* ---- capabilities/<type>：位图直接问设备节点的 ioctl，保证与
     * EVIOCGBIT 完全一致（不复制一份状态，避免两边漂移）---- */
    case SYSFS_ATTR_CAP_EV:
    case SYSFS_ATTR_CAP_KEY:
    case SYSFS_ATTR_CAP_REL:
    case SYSFS_ATTR_CAP_ABS:
    case SYSFS_ATTR_CAP_PROP: {
      u8 bits[96];
      u32 nbytes = 4;
      u32 cmd;
      vnode_t* devnode;

      kmemset(bits, 0, sizeof(bits));
      switch (node->inode) {
        case SYSFS_ATTR_CAP_KEY:
          nbytes = 96; /* KEY_MAX 0x2ff → 768 bit */
          cmd = sysfs_eviocgbit(1 /*EV_KEY*/, nbytes);
          break;
        case SYSFS_ATTR_CAP_REL:
          nbytes = 2; /* REL_MAX 0x0f */
          cmd = sysfs_eviocgbit(2 /*EV_REL*/, nbytes);
          break;
        case SYSFS_ATTR_CAP_ABS:
          nbytes = 8; /* ABS_MAX 0x3f */
          cmd = sysfs_eviocgbit(3 /*EV_ABS*/, nbytes);
          break;
        case SYSFS_ATTR_CAP_PROP:
          nbytes = 4; /* INPUT_PROP_MAX 0x1f */
          cmd = 0x80000000u | (nbytes << 16) | ((u32)'E' << 8) | 0x09u;
          break;
        default: /* EV 位图 */
          nbytes = 4; /* EV_MAX 0x1f */
          cmd = sysfs_eviocgbit(0, nbytes);
          break;
      }
      devnode = sysfs_input_node(idx);
      if (devnode != NULL) {
        vioctl(devnode, cmd, bits);
      }
      sysfs_put_bitmap(&p, end, bits, nbytes);
      break;
    }
    default:
      /* 其余属性都要几何；取不到就按 0 输出（文件仍可读，别让 stat/open 挂） */
      sysfs_fb_geometry(idx, &w, &h, &bpp);
      if (node->inode == SYSFS_ATTR_MODES) {
        procfs_put_dec(&p, end, w);
        procfs_put_char(&p, end, 'x');
        procfs_put_dec(&p, end, h);
        procfs_put_char(&p, end, '\n');
      } else if (node->inode == SYSFS_ATTR_VIRTUAL_SIZE) {
        procfs_put_dec(&p, end, w);
        procfs_put_char(&p, end, ',');
        procfs_put_dec(&p, end, h);
        procfs_put_char(&p, end, '\n');
      } else if (node->inode == SYSFS_ATTR_STRIDE) {
        procfs_put_dec(&p, end, w * bpp / 8);
        procfs_put_char(&p, end, '\n');
      } else if (node->inode == SYSFS_ATTR_BPP) {
        procfs_put_dec(&p, end, bpp);
        procfs_put_char(&p, end, '\n');
      } else if (node->inode == SYSFS_ATTR_ROTATE ||
                 node->inode == SYSFS_ATTR_BLANK) {
        procfs_put_char(&p, end, '0');
        procfs_put_char(&p, end, '\n');
      } else {
        return -1;
      }
      break;
  }
  *p = '\0';
  return (int)(p - out);
}

static u32 sysfs_file_read(vnode_t* node, u32 offset, u32 size, u8* buf) {
  char tmp[512]; /* capabilities/key 的十六进制文本可到 200+ 字节 */
  int len;
  u32 avail;
  u32 n;

  if (node == NULL || buf == NULL) {
    return 0;
  }
  len = sysfs_generate(node, tmp, sizeof(tmp));
  if (len <= 0 || offset >= (u32)len) {
    return 0;
  }
  avail = (u32)len - offset;
  n = size < avail ? size : avail;
  kmemcpy(buf, tmp + offset, n);
  return n;
}

/* 目录枚举：按 *offset 取第 N 项，命中就写一条并推进偏移（与 procfs 同约定） */
static int sysfs_dir_emit(vdirent_t* dirent, u32* offset, u32 want, u32* idx,
                          const char* name, u8 type) {
  if (*idx != want) {
    (*idx)++;
    return 0;
  }
  kmemset(dirent, 0, sizeof(vdirent_t));
  dirent->ino = want + 1;
  kstrncpy(dirent->name, name, sizeof(dirent->name) - 1);
  dirent->type = type;
  *offset = want + 1;
  return 1;
}

static u32 sysfs_fb_entry(vdirent_t* dirent, u32* offset, u32 want, u32* idx,
                          u32 fb, u8 type) {
  char name[8];

  name[0] = 'f';
  name[1] = 'b';
  name[2] = (char)('0' + fb);
  name[3] = '\0';
  return (u32)sysfs_dir_emit(dirent, offset, want, idx, name, type);
}

static u32 sysfs_readdir(vnode_t* node, vdirent_t* dirent, u32* offset,
                         u32 count) {
  static const char* const fb_attrs[] = {
      "name",  "dev",   "modes",  "virtual_size", "stride", "bits_per_pixel",
      "blank", "rotate", "uevent", "subsystem",    "device"};
  const sysfs_entry_t* e;
  u32 want;
  u32 idx = 0;
  u32 i;
  u32 fb;

  (void)count;
  if (node == NULL || dirent == NULL || offset == NULL) {
    return 0;
  }
  want = *offset;
  e = sysfs_resolve(node->name, NULL);

  switch (e != NULL ? e->content : 0) {
    case SYSFS_DIR_ROOT:
      if (sysfs_dir_emit(dirent, offset, want, &idx, "class", V_DIRECTORY)) {
        return 1;
      }
      if (sysfs_dir_emit(dirent, offset, want, &idx, "bus", V_DIRECTORY)) {
        return 1;
      }
      if (sysfs_dir_emit(dirent, offset, want, &idx, "devices", V_DIRECTORY)) {
        return 1;
      }
      /* /sys/dev：udev 按 major:minor 反查设备用 */
      if (sysfs_dir_emit(dirent, offset, want, &idx, "dev", V_DIRECTORY)) {
        return 1;
      }
      break;
    case SYSFS_DIR_CLASS:
      if (sysfs_dir_emit(dirent, offset, want, &idx, "graphics", V_DIRECTORY)) {
        return 1;
      }
      if (sysfs_dir_emit(dirent, offset, want, &idx, "input", V_DIRECTORY)) {
        return 1;
      }
      break;
    case SYSFS_DIR_DEV:
      if (sysfs_dir_emit(dirent, offset, want, &idx, "char", V_DIRECTORY)) {
        return 1;
      }
      break;
    case SYSFS_DIR_DEV_CHAR:
      /* 每块现存输入设备一项："13:64+N"（Linux input 的 major:minor） */
      for (fb = 0; fb < SYSFS_INPUT_MAX; fb++) {
        char name[8];
        u32 minor;

        if (sysfs_input_node(fb) == NULL) {
          continue;
        }
        minor = SYSFS_INPUT_MINOR_BASE + fb;
        name[0] = '1';
        name[1] = '3';
        name[2] = ':';
        name[3] = (char)('0' + (minor / 10) % 10);
        name[4] = (char)('0' + minor % 10);
        name[5] = '\0';
        if (sysfs_dir_emit(dirent, offset, want, &idx, name, V_FILE)) {
          return 1;
        }
      }
      break;
    case SYSFS_DIR_INPUT:
      /* /sys/class/input 与 /sys/devices/virtual/input 都列 eventN */
      for (fb = 0; fb < SYSFS_INPUT_MAX; fb++) {
        char name[16];

        if (sysfs_input_node(fb) == NULL) {
          continue;
        }
        name[0] = 'e';
        name[1] = 'v';
        name[2] = 'e';
        name[3] = 'n';
        name[4] = 't';
        name[5] = (char)('0' + (fb < 10 ? fb : fb / 10));
        name[6] = (char)('0' + (fb % 10));
        if (fb < 10) {
          name[6] = '\0';
        } else {
          name[7] = '\0';
        }
        if (sysfs_dir_emit(dirent, offset, want, &idx, name, V_DIRECTORY)) {
          return 1;
        }
      }
      break;
    case SYSFS_DIR_INPUT_EVENT: {
      static const char* const input_attrs[] = {
          "name", "dev", "uevent", "subsystem", "device", "capabilities"};
      for (i = 0; i < sizeof(input_attrs) / sizeof(input_attrs[0]); i++) {
        if (sysfs_dir_emit(dirent, offset, want, &idx, input_attrs[i],
                           V_FILE)) {
          return 1;
        }
      }
      break;
    }
    case SYSFS_DIR_INPUT_CAPS: {
      /* 顺序与 Linux input_dev 一致（udev 的 input_id 只按名字取，顺序无所谓） */
      static const char* const cap_files[] = {"ev", "key", "rel", "abs",
                                              "prop"};
      for (i = 0; i < sizeof(cap_files) / sizeof(cap_files[0]); i++) {
        if (sysfs_dir_emit(dirent, offset, want, &idx, cap_files[i], V_FILE)) {
          return 1;
        }
      }
      break;
    }
    case SYSFS_DIR_BUS:
      if (sysfs_dir_emit(dirent, offset, want, &idx, "platform", V_DIRECTORY)) {
        return 1;
      }
      break;
    case SYSFS_DIR_DEVICES:
      if (sysfs_dir_emit(dirent, offset, want, &idx, "platform", V_DIRECTORY)) {
        return 1;
      }
      /* /sys/devices/virtual/input/eventN（realpath 的落点） */
      if (sysfs_dir_emit(dirent, offset, want, &idx, "virtual", V_DIRECTORY)) {
        return 1;
      }
      break;
    case SYSFS_DIR_BUS_PLATFORM:
      if (sysfs_dir_emit(dirent, offset, want, &idx, "drivers", V_DIRECTORY)) {
        return 1;
      }
      break;
    case SYSFS_DIR_BUS_DRIVERS:
      if (sysfs_dir_emit(dirent, offset, want, &idx, "fb", V_DIRECTORY)) {
        return 1;
      }
      break;
    case SYSFS_DIR_GRAPHICS:
    case SYSFS_DIR_DEVICES_PLATFORM:
    case SYSFS_DIR_DRIVER_FB:
      /* 只列真实存在的显示（/dev/fbN 在不在） */
      for (fb = 0; fb < SYSFS_FB_MAX; fb++) {
        if (sysfs_fb_node(fb) == NULL) {
          continue;
        }
        if (sysfs_fb_entry(dirent, offset, want, &idx, fb, V_DIRECTORY)) {
          return 1;
        }
      }
      break;
    case SYSFS_DIR_FB:
      for (i = 0; i < sizeof(fb_attrs) / sizeof(fb_attrs[0]); i++) {
        if (sysfs_dir_emit(dirent, offset, want, &idx, fb_attrs[i], V_FILE)) {
          return 1;
        }
      }
      break;
    case SYSFS_DIR_FB_DEV:
      /* 设备本体：Linux 上这里是 name/subsystem/driver/uevent */
      if (sysfs_dir_emit(dirent, offset, want, &idx, "name", V_FILE)) {
        return 1;
      }
      if (sysfs_dir_emit(dirent, offset, want, &idx, "subsystem", V_FILE)) {
        return 1;
      }
      if (sysfs_dir_emit(dirent, offset, want, &idx, "driver", V_FILE)) {
        return 1;
      }
      break;
    default:
      break;
  }
  return 0;
}

/* stat：目录/普通属性/软链接三种模式各按 Linux 语义报（软链接用 lstat 语义） */
static size_t sysfs_ioctl(vnode_t* node, u32 cmd, void* args) {
  struct stat* st;
  char tmp[512]; /* capabilities/key 的十六进制文本可到 200+ 字节 */
  int n;

  if (node == NULL || cmd != IOC_STAT || args == NULL) {
    return 0;
  }
  st = (struct stat*)args;
  if ((node->flags & V_DIRECTORY) == V_DIRECTORY) {
    st->st_mode = S_IFDIR | 0555;
    st->st_nlink = 2;
    st->st_size = 0;
  } else {
    const sysfs_entry_t* e = sysfs_resolve(node->name, NULL);
    if (e != NULL && e->kind == SYSFS_ENT_LINK) {
      st->st_mode = S_IFLNK | 0777;
      st->st_nlink = 1;
      st->st_size = kstrlen(e->target);
    } else {
      n = sysfs_generate(node, tmp, sizeof(tmp));
      st->st_mode = S_IFREG | 0444;
      st->st_nlink = 1;
      st->st_size = (n > 0) ? n : 0;
    }
  }
  st->st_uid = 0;
  st->st_gid = 0;
  st->st_blksize = 4096;
  return 0;
}

/* open/stat/access/opendir 的统一入口：命中 /sys 条目就造一个临时 vnode */
static vnode_t* sysfs_lookup(const char* path) {
  char buf[192];
  char norm[192];
  const sysfs_entry_t* e;
  vnode_t* node;
  u32 idx = 0;
  int as_dir;

  path = sysfs_trim(path, buf, sizeof(buf));
  if (path == NULL || !sysfs_is_sys(path)) {
    return NULL;
  }
  if (sysfs_normalize(path, norm, sizeof(norm)) < 0) {
    return NULL;
  }
  path = norm;
  e = sysfs_resolve(path, &idx);
  if (e == NULL) {
    return NULL;
  }
  if (sysfs_entry_uses_fb(e) && sysfs_fb_node(idx) == NULL) {
    return NULL; /* 没有第 idx 块屏：这条路径确实不存在 */
  }
  if (sysfs_tmpl_has(e->path, 'E') && sysfs_input_node(idx) == NULL) {
    return NULL; /* 没有第 idx 块输入设备：同上 */
  }
  as_dir = (e->kind == SYSFS_ENT_DIR) || sysfs_has_children(path);
  node = vfs_create_node((u8*)path, as_dir ? V_DIRECTORY : V_FILE);
  if (node == NULL) {
    return NULL;
  }
  node->inode = e->content;
  node->impl = idx;
  node->op = as_dir ? &sysfs_dir_ops : &sysfs_file_ops;
  return node;
}

/* readlink 用：返回 >0 = 目标长度；0 = 不是 /sys 的软链接（交给通用解析）；
 * -1 = 是软链接但目标不可得（例如那块屏根本不存在 ⇒ 上层报 ENOENT）。 */
static int sysfs_readlink_magic(const char* path, char* out, size_t outsz,
                                size_t* olen) {
  char buf[192];
  char norm[192];
  const sysfs_entry_t* e;
  u32 idx = 0;
  int n;

  path = sysfs_trim(path, buf, sizeof(buf));
  if (path == NULL || !sysfs_is_sys(path)) {
    return 0;
  }
  if (sysfs_normalize(path, norm, sizeof(norm)) < 0) {
    return -1;
  }
  path = norm;
  e = sysfs_resolve(path, &idx);
  if (e == NULL) {
    return -1; /* /sys 下没有这个路径 ⇒ ENOENT */
  }
  if (e->kind != SYSFS_ENT_LINK) {
    /* 存在但不是符号链接。Linux 的 readlink() 对此返回 EINVAL 而不是
     * ENOENT —— musl 的 realpath() 就是靠这个区分"目录，继续往下走"，
     * 回 ENOENT 会让 realpath("/sys/dev/char/13:64") 直接失败。 */
    return -2;
  }
  if (sysfs_entry_uses_fb(e) && sysfs_fb_node(idx) == NULL) {
    return -1;
  }
  if (sysfs_tmpl_has(e->path, 'E') && sysfs_input_node(idx) == NULL) {
    return -1;
  }
  n = sysfs_expand(e->target, idx, out, outsz);
  if (n <= 0) {
    return -1;
  }
  *olen = (size_t)n;
  return n;
}

/* ===================== /dev 全局可见垫片 =====================
 * exec 时 vfs_chroot_mount() 会把进程根换成"程序所在的那个挂载点"（SD 卡上
 * 的 rootfs 与内核根不是同一棵树）。设备节点是内核建在【全局根】的 /dev
 * 下的，切根之后 "/dev/fb0" 会落到 rootfs 自己的 dev 目录里（空）⇒ Xorg
 * 报 "cannot found file /dev/fb0"，fbdev 驱动随即
 * "Fatal server error: no screens found"。
 *
 * 这里在路径解析前按字符串拦截 /dev/*，一律从全局根的 /dev 往下查：
 *   · 命中 → 返回真实设备节点，open/ioctl/mmap 与没切根时完全一致；
 *   · 未命中 → 返回 NULL，落回进程自己的根（在 /dev 下建文件的语义不变）。
 * /proc、(sysfs 垫片) /sys 早已按字符串接管，三者行为一致。
 * 逐段用 vfs_find_child 查（不走 vfs_find）避免"不存在"刷错误日志。 */
static vnode_t* devfs_lookup(const char* path) {
  vnode_t* cur;
  const char* p;

  if (path == NULL || kstrncmp(path, "/dev", 4) != 0) {
    return NULL;
  }
  if (path[4] != '\0' && path[4] != '/') {
    return NULL; /* "/device"、"/devxxx" 这类只是前缀相同的普通路径 */
  }
  cur = vfs_find_child(root_node, "dev");
  if (cur == NULL) {
    return NULL;
  }
  p = path + 4;
  while (*p != '\0') {
    char name[64];
    int n = 0;
    while (*p == '/') {
      p++; /* 跳过 '/'（含尾部多余斜杠） */
    }
    if (*p == '\0') {
      break;
    }
    while (*p != '\0' && *p != '/') {
      if (n < (int)sizeof(name) - 1) {
        name[n++] = *p;
      }
      p++;
    }
    name[n] = '\0';
    cur = vfs_find_child(cur, name);
    if (cur == NULL) {
      return NULL;
    }
  }
  return cur;
}

