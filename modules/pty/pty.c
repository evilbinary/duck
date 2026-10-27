#include "pty.h"

#include "kernel/error.h" /* EAGAIN：空读返回给 sys_read 走阻塞等待 */
#include "kernel/stat.h"  /* IOC_STAT：common.h 开从端前会 stat 查 uid/mode */
#include "kernel/string.h" /* kmemset */

static vnode_t *pty_clone_open(vnode_t *node);
extern voperator_t pty_master_operator;
extern voperator_t pty_slave_operator;

/* close 必须给：fd_close 走到 op->close 为 NULL 时会打
 * "node ptm close is null" 错误日志，终端每次关窗都要刷一条 */
u32 pty_close(vnode_t *node) {
  pty_t *pty = node->device;
  if (pty == NULL) {
    return 0;
  }
  if (node->op == &pty_master_operator) {
    /* master 没了 ⇒ 从端变死终端：read EOF、write EIO；唤醒可能在
     * V_BLOCK_READ 上挂着的从端读者（pty08 的语义） */
    pty->dead = 1;
    if (thread_wait_notify_hook != NULL) {
      thread_wait_notify_hook();
    }
  } else if (node->op == &pty_slave_operator) {
    if (pty->slave_cnt > 0) {
      pty->slave_cnt--;
    }
  }
  return 0;
}

voperator_t pty_master_operator = {.close = pty_close,
                                   .read = pty_master_read,
                                   .write = pty_master_write,
                                   .open = pty_open,
                                   .ioctl = pty_ioctl,
                                   .clone_open = pty_clone_open};

voperator_t pty_slave_operator = {.close = pty_close,
                                  .read = pty_slave_read,
                                  .write = pty_slave_write,
                                  .open = pty_slave_open,
                                  .ioctl = pty_ioctl};

/* 【形参必须是 void*，不能是 va_list】本函数挂在 voperator_t.ioctl 槽位上
 * （其类型是 void*），而 AArch64 的 va_list 是"结构体"：按 va_list 声明时，
 * 编译器会去【读该指针指向的 32 字节】构造副本 ⇒ 无参 ioctl 传 NULL 就
 * memory fault at 0（和 devfn.c 的 device_ioctl 是同一个坑 ✓）。
 * 本函数并不使用 args ⇒ 直接改成 void* 即可。 */
ssize_t pty_ioctl(vnode_t *node, u32 cmd, void *args) {
  (void)args;
  if (cmd == IOC_SLAVE) {
    pty_t *pty = node->device;
    return pty->slave_no;
  } else if (cmd == IOC_MASTER_READ_NOBLOCK) {
    pty_t *pty = node->device;
    pty->master_read_block = 0;
    return 1;
  } else if (cmd == 0x80045430u) { /* TIOCGPTN：musl openpty/ptsname_r 靠它
                                    * 拿从端编号，再 open /dev/pts/N */
    pty_t *pty = node->device;
    if (args == NULL) {
      return -1;
    }
    *((int *)args) = pty->slave_no;
    return 0;
  } else if (cmd == 0x40045431u) { /* TIOCSPTLCK：unlockpt 关锁，恒无锁 */
    return 0;
  } else if (cmd == 0x5401u || cmd == 0x5405u) { /* TCGETS / TCGETA：行规程
                                                  * 未实现，回全零默认参数；
                                                  * 坏指针必须报 EFAULT */
    if (args == NULL || (long)args == -1) {
      return -EFAULT;
    }
    kmemset(args, 0, cmd == 0x5401u ? 60 : 18);
    return 0;
  } else if (cmd >= 0x5402u && cmd <= 0x5408u) { /* TCSETS/TCSETSW/TCSETSF/
                                                  * TCSETA/TCSETAW/TCSETAF：
                                                  * 设置是 no-op（行规程不
                                                  * 存在），但不能报错 */
    return 0;
  } else if (cmd == 0x5413u) { /* TIOCGWINSZ：回本实例窗口大小 */
    pty_t *pty = node->device;
    if (args == NULL || (long)args == -1) {
      return -EFAULT;
    }
    kmemset(args, 0, 8);
    kmemcpy(args, pty->winsize, sizeof(pty->winsize));
    return 0;
  } else if (cmd == 0x5414u) { /* TIOCSWINSZ：存下，主从端共享 */
    pty_t *pty = node->device;
    if (args == NULL || (long)args == -1) {
      return -EFAULT;
    }
    kmemcpy(pty->winsize, args, sizeof(pty->winsize));
    return 0;
  } else if (cmd >= 0x5409u && cmd <= 0x540Bu) { /* TCSBRK/TCXONC/TCFLSH：
                                                  * break/流控/冲刷均无
                                                  * 硬件语义，no-op 成功 */
    return 0;
  } else if (cmd == 0x541Bu) { /* FIONREAD：可读字节数（pty02） */
    pty_t *pty = node->device;
    if (args == NULL) {
      return -1;
    }
    if (node->op == &pty_master_operator) {
      *((int *)args) = (int)buffer_size(pty->in);
    } else {
      *((int *)args) = (int)buffer_size(pty->out);
    }
    return 0;
  } else if (cmd == 0x5423u) { /* TIOCSETD：线路规程。只支持缺省 N_TTY(0)，
                                * 其余（N_HDLC 等）回 -EINVAL——pty05 以此
                                * 判定"线规程不可用"走 TCONF */
    if (args == NULL) {
      return -1;
    }
    if (*(int *)args != 0) {
      return -EINVAL;
    }
    return 0;
  } else if (cmd == IOC_STAT) {
    /* LTP pty common.h open_slave 会 stat 从端节点：必须是字符设备
     * 0620/0600 且 uid == getuid()，否则 TBROK。 */
    struct stat *st = (struct stat *)args;
    if (st == NULL) {
      return -1;
    }
    kmemset(st, 0, sizeof(*st));
    st->st_mode = S_IFCHR | 0620;
    st->st_uid = 0;
    return 0;
  }
  return -1;
}

u32 pty_open(vnode_t *node, u32 mode) {
  /* clone-on-open 后这里只收尾私有实例节点：从端已在 pty_clone_open 里
   * 产出并登记（每个 master 一个独立 pty_t），不能再走 pty_slave_add */
  (void)node;
  (void)mode;
  return 1;
}

/* 【本内核没有"睡在内核里等数据"的能力】schedule_switch 丢弃内核调用链，
 * 线程被唤醒后回不到 vread 的返回点，所以 op->read 一律不阻塞：
 *   没数据 → 返回 -EAGAIN，由 sys_read 对带 V_BLOCK_READ 的节点走
 *   sys_rw_wait 挂起（与 unix socket 同一套 poll 机制），写入方用
 *   thread_wait_notify_hook 唤醒等待者重试。
 * 缓冲的 wait/notify 回调全部置 NULL（原来的 rw_queue 阻塞就是这个死结），
 * buffer 满/空都在调用方层面短读/短写。 */
u32 pty_master_read(vnode_t *node, u32 offset, u32 size, u8 *buffer) {
  pty_t *pty = node->device;
  if (pty == NULL) {
    kprintf("pyt read null\n");
    return -1;
  }
  if (buffer_size(pty->in) == 0) {
    /* 从端全部关闭且无数据 ⇒ 对端挂断：read 回 -EIO（hangup01 靠它数
     * hangup 次数）；还有从端活着就正常 EAGAIN */
    if (pty->slave_cnt == 0) {
      return (u32)-EIO;
    }
    return (u32)-EAGAIN; /* 主端恒非阻塞：终端主循环轮询着读 */
  }
  return buffer_read(pty->in, buffer, size);
}

u32 pty_master_write(vnode_t *node, u32 offset, u32 size, u8 *buffer) {
  pty_t *pty = node->device;
  if (pty == NULL) {
    kprintf("pyt write null\n");
    return -1;
  }
#ifdef DEBUG_PTY
  kprintf("pty master write %s\n", buffer);
#endif
  u32 ret = buffer_write(pty->out, buffer, size);
  /* 写进数据 ⇒ 从端的阻塞读可就绪：唤醒 poll/rw 等待者（同 pipe 的做法） */
  if (ret > 0 && thread_wait_notify_hook != NULL) {
    thread_wait_notify_hook();
  }
  return ret;
}

u32 pty_slave_read(vnode_t *node, u32 offset, u32 size, u8 *buffer) {
  pty_t *pty = node->device;
  if (pty == NULL) {
    kprintf("pyt slave read null\n");
    return -1;
  }
#ifdef DEBUG_PTY
  kprintf("pty slave read\n");
#endif
  if (pty->out == NULL) {
    kprintf("pty slave read erro for out null\n");
    return -1;
  }
  if (pty->dead) {
    return 0; /* master 已关：EOF（pty08） */
  }
  if (buffer_size(pty->out) == 0) {
    /* 无数据不等待：sys_read 对 V_BLOCK_READ 节点据此挂起（见文件头注释） */
    return (u32)-EAGAIN;
  }
  return buffer_read(pty->out, buffer, size);
}

u32 pty_slave_write(vnode_t *node, u32 offset, u32 size, u8 *buffer) {
  pty_t *pty = node->device;
  if (pty == NULL) {
    kprintf("pyt slave write null\n");
    return -1;
  }
  if (pty->dead) {
    return (u32)-EIO; /* master 已关：写死终端报 EIO（pty08） */
  }
#ifdef DEBUG_PTY
  kprintf("pty slave write %s\n", buffer);
#endif
  u32 ret = buffer_write(pty->in, buffer, size);
  if (ret > 0 && thread_wait_notify_hook != NULL) {
    thread_wait_notify_hook(); /* 唤醒主端等待者（目前主端只轮询，留语义一致） */
  }
  return ret;
}

u32 pty_slave_open(vnode_t *node, u32 mode) {
  (void)mode;
  pty_t *pty = node->device;
  if (pty != NULL) {
    pty->slave_cnt++;
  }
#ifdef DEBUG_PTY
  kprintf("pty slave open %s\n", node->name);
#endif
  return 1;
}

/* 【per-instance】clone-on-open 前 pty_t 是全局单例：所有 master 共享
 * in/out 缓冲，ptem05 开 10 个 master 全拿到同一个从端号（TIOCGPTN 读的
 * 是"当前最新"），pty01 第二次跑还读出上个会话残留的 \n。现在每次 open
 * /dev/ptm[x] 产出一个私有 pty_t，从端节点挂到全局 /dev/pts 下按名索引。 */
static vnode_t *g_pts_entry;

int pty_slave_add(vnode_t *global_pts, pty_t *pty) {
  char buf[16];
  /* 名字用全局 pts 目录的 child_number（第几个从端），IOC_SLAVE/
   * TIOCGPTN 返回 pty->slave_no（本实例在 clone 时定下的号） */
  int i = (int)global_pts->child_number;
  sprintf(buf, "%d", i);
  vnode_t *child = vfs_create_node(buf, V_FILE);
  /* 从端 open/read 都要"空了能等"：打上 V_BLOCK_READ，sys_read 才会挂起等待 */
  child->flags |= V_BLOCK_READ;
  child->device = pty;
  child->op = &pty_slave_operator;
  vfs_add_child(global_pts, child);
  return i;
}

/* clone-on-open：挂在 pty_master_operator.clone_open 上。树里带这个 op 的
 * 只有 /dev/ptm 与 /dev/ptmx 两个入口节点（私有实例节点不进目录树，不会
 * 被解析到），无需再判节点身份 */
static vnode_t *pty_clone_open(vnode_t *node) {
  pty_t *pty = kmalloc(sizeof(pty_t), DEFAULT_TYPE);
  if (pty == NULL) {
    return NULL;
  }
  kmemset(pty, 0, sizeof(pty_t));
  /* wait/notify 全 NULL：本内核不支持"挂在内核调用链里等"（见文件头注释），
   * 空/满一律靠 -EAGAIN + sys_rw_wait + thread_wait_notify_hook。 */
  pty->in = buffer_create(PTY_BUFFER, NULL, NULL, NULL, NULL);
  pty->out = buffer_create(PTY_BUFFER, NULL, NULL, NULL, NULL);

  vnode_t *m = vfs_create_node("ptm", V_DIRECTORY);
  if (m == NULL) {
    return NULL;
  }
  m->op = &pty_master_operator;
  m->device = pty;
  /* 主端保持非阻塞（空读 -EAGAIN 轮询）：xterm 主循环靠这个语义 */
  pty->master = m;
  pty->master_read_block = 1;
  pty->slave_no = pty_slave_add(g_pts_entry, pty);
  return m;
}

int pty_create(vnode_t **ptm, vnode_t **pts) {
  /* 入口节点占位 pty_t：clone-on-open 后真正的实例都在 pty_clone_open 里
   * 建，这里的缓冲不会被读写，仅填结构体字段 */
  pty_t *pty = kmalloc(sizeof(pty_t),DEFAULT_TYPE);
  kmemset(pty, 0, sizeof(pty_t));
  pty->in = buffer_create(PTY_BUFFER, NULL, NULL, NULL, NULL);
  pty->out = buffer_create(PTY_BUFFER, NULL, NULL, NULL, NULL);

  pty->master = vfs_create_node("ptm", V_DIRECTORY);
  pty->slave = vfs_create_node("pts", V_DIRECTORY);

  *ptm = pty->master;
  *pts = pty->slave;

  pty->master->op = &pty_master_operator;

  pty->master_read_block = 1;
  pty->slave->op = &pty_slave_operator;

  pty->slave->device = pty;

  pty->master->device = pty;

  return 1;
}

int pty_init(void) {
  // pty
  vnode_t *pts = NULL;
  vnode_t *ptm = NULL;
  pty_create(&ptm, &pts);
  vfs_mount(NULL, "/dev", ptm);
  vfs_mount(NULL, "/dev", pts);
  g_pts_entry = pts;

  /* /dev/ptmx：musl openpty/posix_openpt 只认这个名字（与 /dev/ptm 同一
   * master operator，clone_open 每次 open 产出私有实例） */
  vnode_t *ptmx = vfs_create_node("ptmx", V_DIRECTORY);
  if (ptmx != NULL) {
    ptmx->op = ptm->op;
    ptmx->device = ptm->device;
    ptmx->flags = ptm->flags;
    vfs_mount(NULL, "/dev", ptmx);
  }
  return 0;
}

void pty_exit(void) { kprintf("pty exit\n"); }

module_t pty_module = {.name = "pty", .init = pty_init, .exit = pty_exit};
