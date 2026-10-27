/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef VFS_H
#define VFS_H

#include "arch/arch.h"
#include "config.h"
#include "kernel/logger.h"
#include "kernel/memory.h"
#include "kernel/stdarg.h"
#include "types.h"

#define V_FILE 0x01
#define V_DIRECTORY 0x02
#define V_CHARDEVICE 0x04
#define V_BLOCKDEVICE 0x08
#define V_PIPE 0x0c
#define V_UNLINKED 0x100 /* 文件已从目录删除（LTP IPC mmap 后 unlink），vwrite 拒绝 */
/* 阻塞读设备节点：pty 从端这类"无数据时等待"的节点。op->read 本身不阻塞
 * （空了回 -EAGAIN），sys_read 看到 -EAGAIN 且节点带此标志时走 sys_rw_wait
 * 挂起，由写入方 thread_wait_notify 唤醒重试 —— 和 unix socket 同一套机制。
 * 不能在 op->read 里 thread_wait：本内核 schedule_switch 会丢弃内核调用链，
 * 挂在 vread 中间的线程被唤醒后回不到 syscall 返回点，数据也送不回去。 */
#define V_BLOCK_READ 0x200
/* 符号链接节点约定：data = kmalloc 的链接目标字符串（NUL 结尾），
 * length = 目标字节数。sys_readlink 只对 flags 为 V_SYMLINK 的节点返回目标，
 * 其他类型按 POSIX 返回 EINVAL。 */
#define V_SYMLINK 0x14
#define V_MOUNTPOINT 0x18

struct vnode;
typedef struct vnode vnode_t;

typedef struct vdirent {
  u64 ino;  // Inode number. Required by POSIX.
  u64 offset;
  u16 length;      // Length of this record
  u8 type;         // Type of file
  char name[256];  // Filename.
} vdirent_t;

typedef u32 (*vread_t)(struct vnode *, u32, u32, u8 *);
typedef u32 (*vwrite_t)(struct vnode *, u32, u32, u8 *);
typedef u32 (*vopen_t)(struct vnode *, u32 mode);
typedef u32 (*vclose_t)(struct vnode *);
typedef size_t (*vioctl_t)(struct vnode *, u32 cmd, void *args);

typedef u32 (*vreaddir_t)(struct vnode *, struct vdirent *, u32* , u32);
typedef struct vnode *(*vfinddir_t)(struct vnode *, char *name);
typedef struct vnode *(*vfind_t)(struct vnode *, char *name);
typedef void (*vmount_t)(struct vnode *root, u8 *path, vnode_t *node);
typedef int (*vmkdir_t)(struct vnode *dir, const char *name, u32 mode);
typedef int (*vunlink_t)(struct vnode *dir, const char *name);

typedef struct voperator {
  vread_t read;
  vwrite_t write;
  vopen_t open;
  vclose_t close;
  vreaddir_t readdir;
  vfinddir_t finddir;
  vioctl_t ioctl;
  vfind_t find;
  vmount_t mount;
  /* 【新增】目录操作：LTP tst_tmpdir 需要 mkdir/删除；未实现的文件系统
   * 留 NULL（designated initializer 兼容旧表），dispatch 层返回 -1 */
  vmkdir_t mkdir;
  vunlink_t unlink;
  /* 【新增·必须追加在末尾】同一目录内的改名（rename），空 = 该文件系统
   * 不支持。Xorg 的 LogInit()/锁文件都要用到 rename，此前 sys_rename 是
   * 恒返回 -1 的 stub，导致 "Cannot move old log file" 直接 FatalError。 */
  int (*rename)(struct vnode *dir, const char *oldname, const char *newname);
} voperator_t;

typedef struct vnode {
  char *name;  // The filename.
  u32 mask;    // The permissions mask.
  u32 uid;     // The owning user.
  u32 gid;     // The owning group.
  u32 flags;   // Includes the node type. See #defines above.
  u32 inode;   // This is device-specific - provides a way for a filesystem to
               // identify files.
  u32 length;  // Size of the file, in bytes.
  u32 impl;    // An implementation-defined number.
  vnode_t *parent;  // Used by mountpoints and symlinks.
  vnode_t **child;
  vnode_t *super;
  size_t child_number; // avalible
  size_t child_size; //current size
  void *data;
  void *device;
  voperator_t *op;
} vnode_t;

typedef struct vfs {
  int users;
  vnode_t *root;
  vnode_t *pwd;
} vfs_t;

u32 vread(vnode_t *node, u32 offset, u32 size, u8 *buffer);
u32 vwrite(vnode_t *node, u32 offset, u32 size, u8 *buffer);
u32 vopen(vnode_t *node, u32 mode);
u32 vclose(vnode_t *node);
u32 vreaddir(vnode_t *node, vdirent_t *dirent, u32* offset, u32 count);
/* 直接在父节点的 child 表里按名字查（不走文件系统 op）。
 * 必须在这里声明：vfs.c 里 vunlink 会用它做"删后清缓存"，缺声明时编译器
 * 按隐式 int 处理返回值 ⇒ 指针被截断，一解引用就 data abort。 */
vnode_t *vfs_find_child(vnode_t *parent, char *name);
vnode_t *vfinddir(vnode_t *node, char *name);
vnode_t *vfind(vnode_t *node, char *name);
int vmkdir(vnode_t *dir, const char *name, u32 mode);
int vunlink(vnode_t *dir, const char *name);
/* 同一目录内改名（不支持的文件系统返回 -1） */
int vrename(vnode_t *dir, const char *oldname, const char *newname);
vnode_t *vcreate(u8 *name, u32 flags);
void vmount(vnode_t *node, u8 *path, vnode_t *node1);
size_t vioctl(vnode_t *node, u32 cmd, void *args);

vnode_t *vfs_find(vnode_t *root, u8 *path);
vnode_t *vfs_find_relative(vnode_t *root, vnode_t *pwd, const char *path);
/* 执行挂在某个分区上的程序时，把当前进程的根换成那个挂载点。
 * 先拆开和父进程共用的 vfs，shell 的根不变。不在挂载点上则什么都不做。 */
int vfs_chroot_mount(const char *path);
int vfs_node_is_valid(vnode_t *node);
typedef struct thread thread_t;
int vfs_locked_by(thread_t *t);
int vfs_locked_any(void);
int vfs_ready(void);
int vfs_normalize_path(char *result, const char *path);
void vfs_mount(vnode_t *root, u8 *path, vnode_t *node);
void vfs_add_child(vnode_t *parent, vnode_t *child);
void vfs_remove_child(vnode_t *parent, vnode_t *child);
void vfs_exten_child(vnode_t *node);
int vfs_init();
int vfs_close(vnode_t *node);
u32 vfs_open(vnode_t *node, u32 mode);
vnode_t *vfs_open_attr(vnode_t *root, u8 *name, u32 attr);
u32 vfs_readdir(vnode_t *node, vdirent_t *dirent,u32 * offset, u32 count);
vnode_t *vfs_create_node(u8 *name, u32 flags);
u32 vfs_write(vnode_t *node, u32 offset, u32 size, u8 *buffer);
u32 vfs_read(vnode_t *node, u32 offset, u32 size, u8 *buffer);

#endif