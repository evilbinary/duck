/* 事件类 fd：eventfd2/epoll/inotify/timerfd（哨兵 vnode + fd 模式） */
#include "sysfn.h"

#include "kernel/error.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "kernel/schedule.h"
#include "kernel/thread.h"

#define log_debug

/* ===================== 事件类节点的 close =====================
 * eventfd/epoll/inotify 的 vnode 都是临时对象：vfs_create_node（node +
 * name）+ 私有 device 结构 + 每节点一个 voperator。三者此前都没挂
 * op->close，fd 关闭时 vclose 报 "node epoll close is null"，
 * 且这三块堆内存全部泄漏（Xorg 每轮初始化都会建 epoll/inotify）。 */
static u32 evfd_node_close(vnode_t* node) {
  if (node == NULL) {
    return 0;
  }
  if (node->device != NULL) {
    kfree(node->device);
    node->device = NULL;
  }
  if (node->op != NULL) {
    kfree(node->op);
    node->op = NULL;
  }
  if (node->name != NULL) {
    kfree(node->name);
    node->name = NULL;
  }
  kfree(node);
  return 0;
}

/* ===================== eventfd2(19) ===================== */
struct eventfd_k {
  u32 magic;
  u64 count;
};
#define EVFD_MAGIC 0xEFDC00u

static u32 evfd_read(vnode_t* node, u32 offset, u32 size, u8* buffer) {
  struct eventfd_k* e;
  u64 v;
  (void)offset;
  if (node == NULL || node->device == NULL || buffer == NULL || size < 8) {
    return 0;
  }
  e = (struct eventfd_k*)node->device;
  if (e->magic != EVFD_MAGIC || e->count == 0) {
    return 0; /* 非阻塞：无计数返回 0 */
  }
  v = e->count;
  e->count = 0;
  kmemcpy(buffer, &v, 8);
  return 8;
}

static u32 evfd_write(vnode_t* node, u32 offset, u32 size, u8* buffer) {
  struct eventfd_k* e;
  u64 v;
  (void)offset;
  if (node == NULL || node->device == NULL || buffer == NULL || size < 8) {
    return 0;
  }
  e = (struct eventfd_k*)node->device;
  if (e->magic != EVFD_MAGIC) {
    return 0;
  }
  kmemcpy(&v, buffer, 8);
  if (v == 0) {
    v = 1; /* write(0) = 唤醒语义 */
  }
  e->count += v;
  return 8;
}

/* 供 sys_fd_ready_t 使用：-1 表示"不是 eventfd"。
 * eventfd 的就绪判据是"计数 > 0"。不接住它就会落到 sys_fd_ready_t 最后那条
 * "其它 fd 存在即就绪"兜底 ⇒ epoll/select 永远立刻返回、read 却拿到 0，
 * 等待者（weston 的事件循环）100% 空转。 */
int eventfd_node_ready(vnode_t* node) {
  struct eventfd_k* e;
  if (node == NULL || node->device == NULL) {
    return -1;
  }
  e = (struct eventfd_k*)node->device;
  if (e->magic != EVFD_MAGIC) {
    return -1;
  }
  return e->count > 0 ? 1 : 0;
}

static vnode_t* eventfd_make(u64 initval) {
  vnode_t* node = vfs_create_node("eventfd", 0);
  struct eventfd_k* e = kmalloc(sizeof(struct eventfd_k), KERNEL_TYPE);
  voperator_t* op = kmalloc(sizeof(voperator_t), KERNEL_TYPE);
  if (node == NULL || e == NULL || op == NULL) {
    kfree(e);
    kfree(op);
    return NULL;
  }
  e->count = initval;
  e->magic = EVFD_MAGIC;
  node->device = e;
  node->op = op;
  op->read = evfd_read;
  op->write = evfd_write;
  op->close = evfd_node_close;
  return node;
}

int sys_eventfd2(u64 initval, int flags) {
  thread_t* current = thread_current();
  vnode_t* node = eventfd_make(initval);
  fd_t* fd;
  int n;
  (void)flags;
  if (node == NULL) {
    return -12;
  }
  fd = fd_open(node, DEVICE_TYPE_VIRTUAL, "/dev/eventfd");
  if (fd == NULL) {
    return -12;
  }
  n = thread_add_fd(current, fd);
  return n;
}

/* ===================== signalfd4(74) =====================
 * 把一个信号集变成可读 fd：read 返回 struct signalfd_siginfo(128B)，无信号时
 * 阻塞（本内核非阻塞返回 0，由 sys_read 的 V_BLOCK_READ 机制补齐）。
 *
 * 本内核目前没有异步信号投递（除 SIGKILL 走 thread_stop），所以 pending 恒为 0。
 * 关键是【就绪判定】：signalfd 不属于 pipe/unix socket/evdev，会落到
 * sys_fd_ready_t 的"其它 fd 存在即就绪"兜底 ⇒ epoll_wait 立刻返回、read 拿
 * 不到数据 ⇒ 等待者空转。因此这里导出 signalfd_node_ready()，让就绪判定给出
 * 真实答案（未就绪）。等真信号投递落地后，只需在投递路径里给 pending++ 并
 * 唤醒等待者。 */
#define SIGFD_MAGIC 0x516FD00u
#define SIGFD_SIGINFO_SIZE 128

struct signalfd_k {
  u32 magic;
  u64 mask;
  u32 pending; /* 已投递未读取的信号条数（当前恒 0） */
};

static u32 sigfd_read(vnode_t* node, u32 offset, u32 size, u8* buffer) {
  struct signalfd_k* s;
  (void)offset;
  if (node == NULL || node->device == NULL || buffer == NULL) {
    return 0;
  }
  s = (struct signalfd_k*)node->device;
  if (s->magic != SIGFD_MAGIC || s->pending == 0) {
    return 0; /* 无待取信号 */
  }
  s->pending--;
  kmemset(buffer, 0, size < SIGFD_SIGINFO_SIZE ? size : SIGFD_SIGINFO_SIZE);
  return (size < SIGFD_SIGINFO_SIZE) ? size : SIGFD_SIGINFO_SIZE;
}

/* 供 sys_fd_ready_t 使用：-1 表示"不是 signalfd" */
int signalfd_node_ready(vnode_t* node) {
  struct signalfd_k* s;
  if (node == NULL || node->device == NULL) {
    return -1;
  }
  s = (struct signalfd_k*)node->device;
  if (s->magic != SIGFD_MAGIC) {
    return -1;
  }
  return s->pending > 0 ? 1 : 0;
}

static vnode_t* sigfd_make(u64 mask) {
  vnode_t* node = vfs_create_node("signalfd", 0);
  struct signalfd_k* s = kmalloc(sizeof(struct signalfd_k), KERNEL_TYPE);
  voperator_t* op = kmalloc(sizeof(voperator_t), KERNEL_TYPE);
  if (node == NULL || s == NULL || op == NULL) {
    kfree(s);
    kfree(op);
    return NULL;
  }
  kmemset(s, 0, sizeof(struct signalfd_k));
  s->magic = SIGFD_MAGIC;
  s->mask = mask;
  node->device = s;
  node->op = op;
  op->read = sigfd_read;
  op->close = evfd_node_close;
  return node;
}

int sys_signalfd4(int fd, void* mask, size_t sizemask, int flags) {
  thread_t* current = thread_current();
  u64 kmask = 0;
  u32 n;
  (void)flags;
  if (mask != NULL) {
    n = (sizemask > 0 && sizemask <= sizeof(u64)) ? (u32)sizemask : 8;
    if (sys_copy_from_user(&kmask, mask, n) < 0) {
      return -14;
    }
  }
  if (fd >= 0) {
    /* 更新已有 signalfd 的掩码（Linux 语义：fd 由调用方传入） */
    fd_t* f = thread_find_fd_id(current, fd);
    struct signalfd_k* s;
    if (f == NULL || f->data == NULL) {
      return -9;
    }
    s = (struct signalfd_k*)((vnode_t*)f->data)->device;
    if (s == NULL || s->magic != SIGFD_MAGIC) {
      return -22;
    }
    s->mask = kmask;
    return fd;
  }
  {
    vnode_t* node = sigfd_make(kmask);
    fd_t* nfd;
    if (node == NULL) {
      return -12;
    }
    nfd = fd_open(node, DEVICE_TYPE_VIRTUAL, "/dev/signalfd");
    if (nfd == NULL) {
      evfd_node_close(node);
      return -12;
    }
    return thread_add_fd(current, nfd);
  }
}

/* ===================== epoll 三件套(20/21/22) ===================== */
/* "fd 存在即就绪"模型：epoll_wait 扫描注册项，目标 fd 还在调用者表里
 * 就算 ready（与 select/ppoll 同级）。 */
#define EPOLL_K_MAGIC 0xE901F000u
#define EPOLL_MAX_ITEMS 64

struct epoll_item_k {
  int fd;
  int used;
  u32 events;
  u64 data;
};

struct epoll_k {
  u32 magic;
  int nitems;
  struct epoll_item_k items[EPOLL_MAX_ITEMS];
};

/* 从 fd 表取哨兵 vnode 的私有数据（校验 magic） */
static void* evfd_priv(int fd, u32 magic) {
  thread_t* current = thread_current();
  fd_t* f = thread_find_fd_id(current, fd);
  vnode_t* node;
  if (f == NULL || f->data == NULL) {
    return NULL;
  }
  node = (vnode_t*)f->data;
  if (node->device == NULL) {
    return NULL;
  }
  if (((u32*)node->device)[0] != magic) {
    return NULL;
  }
  return node->device;
}

static vnode_t* epoll_make(void) {
  vnode_t* node = vfs_create_node("epoll", 0);
  struct epoll_k* ep = kmalloc(sizeof(struct epoll_k), KERNEL_TYPE);
  voperator_t* op = kmalloc(sizeof(voperator_t), KERNEL_TYPE);
  if (node == NULL || ep == NULL || op == NULL) {
    kfree(ep);
    kfree(op);
    return NULL;
  }
  kmemset(ep, 0, sizeof(struct epoll_k));
  ep->magic = EPOLL_K_MAGIC;
  node->device = ep;
  node->op = op; /* read/write 留 NULL：epoll fd 只经 epoll_* 访问 */
  op->close = evfd_node_close;
  return node;
}

int sys_epoll_create1(int flags) {
  thread_t* current = thread_current();
  vnode_t* node = epoll_make();
  fd_t* fd;
  int n;
  (void)flags;
  if (node == NULL) {
    return -12;
  }
  fd = fd_open(node, DEVICE_TYPE_VIRTUAL, "/dev/epoll");
  if (fd == NULL) {
    return -12;
  }
  n = thread_add_fd(current, fd);
  return n;
}

/* epoll_event 用户态布局（aarch64）：Linux uapi 里 `EPOLL_PACKED` **只在
 * x86_64 定义**，其它架构就是普通对齐结构：{u32 events; u32 pad; u64 data}
 * ⇒ 16 字节，data 在 offset 8。实测 dump 用户内存证实 w0=events、w2=data。
 * （别按 packed 12 字节读：那样拿到的 data 是 padding，Xorg 的
 * ospoll 会拿野指针当 osfd 用，主线程直接 data abort 退出。） */
struct epoll_event_k {
  u32 events;
  u32 pad;
  u64 data;
};

int sys_epoll_ctl(int epfd, int op, int fd, void* ev) {
  struct epoll_k* ep = (struct epoll_k*)evfd_priv(epfd, EPOLL_K_MAGIC);
  struct epoll_event_k kev;
  int i;
  if (ep == NULL) {
    return -9; /* -EBADF */
  }
  /* 【常量必须与 Linux 一致】EPOLL_CTL_ADD=1、EPOLL_CTL_DEL=2、
   * EPOLL_CTL_MOD=3。旧代码把 DEL/MOD 写反（op==2 当 MOD、op==3 当 DEL），
   * 于是 Xorg 对"刚 accept 的连接"做 MOD 时被当成 DEL 摘出集合 ⇒ 连接被
   * 遗忘，客户端永远收不到 setup 回复（xsetroot 在 ppoll/recvfrom 上空转
   * 烧 45% CPU）。之前给监听 fd 加的"DEL 保留"特例正是这个错位的副作用，
   * 现已一并去掉。 */
  if (op == 2 /* EPOLL_CTL_DEL */) {
    for (i = 0; i < EPOLL_MAX_ITEMS; i++) {
      if (ep->items[i].used && ep->items[i].fd == fd) {
        ep->items[i].used = 0;
        ep->nitems--;
        return 0;
      }
    }
    return -2; /* -ENOENT：syscall 约定是负 errno（返回 +2 时 libc 当成成功值） */
  }
  if (ev != NULL && sys_copy_from_user(&kev, ev, sizeof(kev)) < 0) {
    return -14;
  }
  if (op == 3 /* EPOLL_CTL_MOD */) {
    for (i = 0; i < EPOLL_MAX_ITEMS; i++) {
      if (ep->items[i].used && ep->items[i].fd == fd) {
        ep->items[i].events = kev.events;
        ep->items[i].data = kev.data;
        return 0;
      }
    }
    return -2; /* -ENOENT */
  }
  if (op == 1 /* EPOLL_CTL_ADD */) {
    if (ep->nitems >= EPOLL_MAX_ITEMS) {
      return -12;
    }
    for (i = 0; i < EPOLL_MAX_ITEMS; i++) {
      if (!ep->items[i].used) {
        ep->items[i].used = 1;
        ep->items[i].fd = fd;
        ep->items[i].events = kev.events;
        ep->items[i].data = kev.data;
        ep->nitems++;
        return 0;
      }
    }
  }
  return -22;
}

/* epoll 事件位（与用户态 EPOLLIN/EPOLLOUT 一致） */
#define EPOLLIN_ 0x001
#define EPOLLOUT_ 0x004

/* ===== epoll_pwait 的等待参数（注册给 poll_wait_suspend）=====
 * 直接引用内核 epoll 对象：等待者自己睡着，期间没人能 close 掉最后一份
 * fd 引用（close 只减 use_count，vclose 要等引用归零），指针不会悬空。 */
struct pollw_epoll {
  thread_t* owner;
  int epfd;
  void* uevents;
  int maxevents;
  struct epoll_k* ep;
  u64 saved_blocked; /* epoll_pwait 的临时 sigmask 之前的值（返回时恢复） */
};

/* 在等待者页表下执行（切页表由 poll_wait_suspend/poll_wait_notify 负责） */
static int pollw_epoll_recheck(void* a) {
  struct pollw_epoll* s = (struct pollw_epoll*)a;
  struct epoll_event_k out;
  int ready = 0;
  int i;
  if (thread_find_fd_id(s->owner, s->epfd) == NULL) {
    return -9; /* epfd 已关：唤醒并让 epoll_wait 返回 EBADF */
  }
  for (i = 0; i < EPOLL_MAX_ITEMS; i++) {
    int want;
    int want_r;
    int want_w;
    int ok_r;
    int ok_w;
    if (!s->ep->items[i].used) {
      continue;
    }
    if (thread_find_fd_id(s->owner, s->ep->items[i].fd) == NULL) {
      continue; /* 目标 fd 已关闭 */
    }
    want = s->ep->items[i].events;
    want_r = (want & EPOLLIN_) != 0;
    want_w = (want & EPOLLOUT_) != 0;
    if (!want_r && !want_w) {
      want_r = 1; /* 只关心 ERR/HUP：按可读侧判 */
    }
    ok_r = want_r ? sys_fd_ready_t(s->owner, s->ep->items[i].fd, 1, 0) : 0;
    ok_w = want_w ? sys_fd_ready_t(s->owner, s->ep->items[i].fd, 0, 1) : 0;
    if (!ok_r && !ok_w) {
      continue;
    }
    out.events = (ok_r ? EPOLLIN_ : 0) | (ok_w ? EPOLLOUT_ : 0);
    out.pad = 0;
    out.data = s->ep->items[i].data;
    sys_copy_to_user((void*)((char*)s->uevents + ready * sizeof(out)), &out,
                     sizeof(out));
    ready++;
    if (ready >= s->maxevents) {
      break;
    }
  }
  return ready;
}

static void pollw_epoll_release(void* a) {
  /* 【恢复 epoll_pwait 的临时 sigmask】Linux 语义：epoll_pwait 的 sigmask 只在
   * 本次调用期间【替换】当前掩码，返回时恢复原值。ppoll 走的是同一套协议
   * （见 sys_ppoll / pollw_poll_release）。
   * 屏蔽期间攒下的 pending 位不在这里消费：解除屏蔽后要真正执行 handler
   * （见 sysfn_signal.c 的投递钩子）。 */
  struct pollw_epoll* s = (struct pollw_epoll*)a;
  if (s == NULL) {
    return;
  }
  if (s->owner != NULL) {
    s->owner->sig_blocked = s->saved_blocked;
  }
  kfree(a);
}

/* 就绪判据必须真实：原先"注册的 fd 还在表里就算 ready"会让
 * ospoll_wait/epoll_wait 永不阻塞地立刻返回 ⇒ Xorg 的 InputThread 拿着
 * 假事件空转（实测 400 次/秒 write 一个字节进 hotplug 管道、烧 24% CPU），
 * 主线程也在 epoll_pwait 里 300+ 次/秒打转。这里改走 sys_fd_ready：
 * pipe/本机 socket 按真实缓冲状态，普通文件/网络 socket 仍是"存在即就绪"。 */
int sys_epoll_pwait(int epfd, void* events, int maxevents, int timeout,
                    void* sigmask, size_t sigsetsize) {
  struct epoll_k* ep = (struct epoll_k*)evfd_priv(epfd, EPOLL_K_MAGIC);
  struct epoll_event_k out;
  int ready = 0;
  int i;
  (void)sigsetsize;
  if (ep == NULL) {
    /* fd 不存在 ⇒ EBADF；fd 存在但不是 epoll ⇒ EINVAL（Linux 语义，
     * LTP epoll_wait03 逐条校验这两条）。 */
    return (thread_find_fd_id(thread_current(), epfd) == NULL) ? -9 : -22;
  }
  if (events == NULL || maxevents <= 0) {
    return -22;
  }
  /* 用户指针必须可访问：events 可写（EFAULT），sigmask 可读（EFAULT）。
   * 探针/越界访问在用户态会变成内核 fault，这里提前用 copy 校验。 */
  {
    u8 probe = 0;
    if (sys_copy_to_user(events, &probe, 1) < 0) {
      return -14;
    }
    if (sigmask != NULL) {
      u8 sm[16];
      u32 n = (sigsetsize > 0 && sigsetsize <= sizeof(sm)) ? (u32)sigsetsize : 8;
      if (sys_copy_from_user(sm, sigmask, n) < 0) {
        return -14;
      }
    }
  }
  for (i = 0; i < EPOLL_MAX_ITEMS && ready < maxevents; i++) {
    u32 want;
    int want_r;
    int want_w;
    int ok_r;
    int ok_w;
    if (!ep->items[i].used) {
      continue;
    }
    /* 目标 fd 已被关闭：跳过（下次 ctl 会清掉） */
    if (thread_find_fd_id(thread_current(), ep->items[i].fd) == NULL) {
      continue;
    }
    want = ep->items[i].events;
    want_r = (want & EPOLLIN_) != 0;
    want_w = (want & EPOLLOUT_) != 0;
    if (!want_r && !want_w) {
      want_r = 1; /* 只关心 ERR/HUP 之类：按可读侧判 */
    }
    ok_r = want_r ? sys_fd_ready(ep->items[i].fd, 1, 0) : 0;
    ok_w = want_w ? sys_fd_ready(ep->items[i].fd, 0, 1) : 0;
    if (!ok_r && !ok_w) {
      continue;
    }
    out.events = (ok_r ? EPOLLIN_ : 0) | (ok_w ? EPOLLOUT_ : 0);
    out.pad = 0;
    out.data = ep->items[i].data;
    sys_copy_to_user((void*)((char*)events + ready * sizeof(out)), &out,
                     sizeof(out));
    ready++;
  }
  if (ready > 0) {
    return ready;
  }
  if (timeout == 0) {
    return 0; /* 非阻塞轮询：立刻返回（Linux 语义） */
  }
  /* 【轮询型设备：无限/长超时切成短睡】见 sysfn.h 的
   * SYS_POLLED_FD_RECHECK_MS：这类设备只在被读时才问硬件，没人能主动
   * 唤醒等待者；切成短睡后到期返回 0，调用方重进一次即重新问一次。 */
  for (i = 0; i < EPOLL_MAX_ITEMS; i++) {
    if (!ep->items[i].used) {
      continue;
    }
    if (!sys_fd_polled_t(thread_current(), ep->items[i].fd)) {
      continue;
    }
    if (timeout < 0 || timeout > SYS_POLLED_FD_RECHECK_MS) {
      timeout = SYS_POLLED_FD_RECHECK_MS;
    }
    break;
  }
  /* 无就绪：真正挂起（THREAD_WAITING + schedule_switch），由 unix/pipe 的
   * 事件唤醒并重算 —— 与 select/ppoll 共用同一套协议（sysfn_pollwait.c）。 */
  {
    u64 saved_blocked = thread_current()->sig_blocked;
    u64 sm = 0;
    struct pollw_epoll* pw;
    /* 【临时 sigmask（替换语义）】epoll_pwait 的第 5 参：本次调用期间用
     * sigmask 取代当前掩码，返回时由 pollw_epoll_release 恢复。
     * LTP epoll_pwait01 的"屏蔽 SIGUSR1 后写 pipe"用例就靠它：信号被挡住只
     * 挂 pending，pipe 可读才唤醒并返回 1；不实现时信号 10ms 后直接把等待
     * 打成 -EINTR（实测 TFAIL "returned -1, expected 1"）。
     * 只在"确实要睡"这条路径上替换（其它早返回路径不碰掩码），代价是入口到
     * 这里之间存在极窄的窗口 —— 本内核不执行 handler，可接受。 */
    if (sigmask != NULL) {
      if (sys_copy_from_user(&sm, sigmask, sizeof(sm)) < 0) {
        return -14; /* -EFAULT */
      }
      thread_current()->sig_blocked = sm;
    }
    pw = kmalloc(sizeof(*pw), KERNEL_TYPE);
    if (pw == NULL) {
      thread_current()->sig_blocked = saved_blocked;
      return -12;
    }
    pw->owner = thread_current();
    pw->epfd = epfd;
    pw->uevents = events;
    pw->maxevents = maxevents;
    pw->ep = ep;
    pw->saved_blocked = saved_blocked;
    return poll_wait_suspend(
        pollw_epoll_recheck, NULL, pollw_epoll_release, pw,
        (u32)(timeout > 0x7fffffff ? 0x7fffffff : timeout), (timeout > 0));
  }
}

/* epoll_pwait2(441)：与 epoll_pwait 相同，但超时是 timespec（纳秒精度）。
 * glibc 2.35+ 的 epoll_wait/epoll_pwait 优先走它：缺失时每次调用都要先吃
 * 一次 ENOSYS 再 fallback，errno 还会残留在调用方（观察到 Xorg 的
 * epoll_wait 因此拿不到事件）。超时 ms 向上取整，避免"早醒"。 */
int sys_epoll_pwait2(int epfd, void* events, int maxevents, void* tmo,
                     void* sigmask, size_t sigsetsize) {
  int timeout = -1;
  if (tmo != NULL) {
    i64 sec64 = *(volatile i64*)tmo;
    i64 nsec64 = *(volatile i64*)((char*)tmo + 8);
    if (sec64 < 0 || nsec64 < 0 || nsec64 >= 1000000000ll) {
      return -22; /* EINVAL，Linux 对负值/越界 nsec 的行为 */
    }
    timeout = (int)(sec64 * 1000ll + (nsec64 + 999999ll) / 1000000ll);
    if (timeout < 0) {
      timeout = 0;
    }
  }
  return sys_epoll_pwait(epfd, events, maxevents, timeout, sigmask, sigsetsize);
}

/* ===================== inotify(26/27/28) ===================== */
/* 最小实现：fd/句柄真实创建（调用方初始化可容错），不产生事件。 */
struct inotify_k {
  u32 magic;
  int wd_next;
};
#define INOTIFY_K_MAGIC 0x1B07u

static vnode_t* inotify_make(void) {
  vnode_t* node = vfs_create_node("inotify", 0);
  struct inotify_k* in = kmalloc(sizeof(struct inotify_k), KERNEL_TYPE);
  voperator_t* op = kmalloc(sizeof(voperator_t), KERNEL_TYPE);
  if (node == NULL || in == NULL || op == NULL) {
    kfree(in);
    kfree(op);
    return NULL;
  }
  in->magic = INOTIFY_K_MAGIC;
  in->wd_next = 1;
  node->device = in;
  node->op = op;
  op->close = evfd_node_close;
  return node;
}

int sys_inotify_init1(int flags) {
  thread_t* current = thread_current();
  vnode_t* node = inotify_make();
  fd_t* fd;
  int n;
  (void)flags;
  if (node == NULL) {
    return -12;
  }
  fd = fd_open(node, DEVICE_TYPE_VIRTUAL, "/dev/inotify");
  if (fd == NULL) {
    return -12;
  }
  n = thread_add_fd(current, fd);
  return n;
}

int sys_inotify_add_watch(int fd, const char* pathname, u32 mask) {
  struct inotify_k* in = (struct inotify_k*)evfd_priv(fd, INOTIFY_K_MAGIC);
  (void)pathname;
  (void)mask;
  if (in == NULL) {
    return -9;
  }
  return in->wd_next++; /* 返回假 wd，监视不落地 */
}

int sys_inotify_rm_watch(int fd, int wd) {
  struct inotify_k* in = (struct inotify_k*)evfd_priv(fd, INOTIFY_K_MAGIC);
  (void)wd;
  if (in == NULL) {
    return -9;
  }
  return 0;
}

/* ===================== timerfd 三件套(85/86/87) =====================
 * 用途：libinput 的定时器子系统（src/timer.c）用
 * timerfd_create(CLOCK_MONOTONIC) + timerfd_settime(TFD_TIMER_ABSTIME)
 * 驱动 tap/滚动/长按等超时逻辑，缺失时 libinput_init 直接失败。
 *
 * 记账时基与 clock_gettime(CLOCK_MONOTONIC) 一致（schedule_get_ticks），
 * 单位毫秒：SCHEDULE_FREQUENCY 通常就是 1000，这里按宏换算，不假设 tick=ms。
 *
 * 【就绪怎么被发现】本内核的 syscall 不能睡在内核里（见 sysfn_pollwait.c），
 * 所以 timerfd 不注册定时中断回调，而是复用 evdev 那套"SYS_POLLED_FD_RECHECK_MS
 * 切片重查"：调用方的 select/ppoll/epoll_pwait 长超时被切成 8ms 一片，每片
 * 都调 sys_fd_ready_t 重算一次 ⇒ 到期最迟 8ms 后被唤醒。代价是空闲期每 8ms
 * 醒一次，收益是不必引入内核定时器回调链路。 */
struct timerfd_k {
  u32 magic;
  u64 next_ms;     /* 下次到期的单调绝对毫秒；0 = 未武装 */
  u64 interval_ms; /* 0 = 一次性 */
  u64 expirations; /* 已到期但还没被 read 取走的次数 */
};
#define TIMERFD_MAGIC 0x71FD0000u

/* aarch64/musl 的 struct itimerspec：it_interval 在前，it_value 在后 */
struct tfd_itimerspec {
  i64 interval_sec;
  i64 interval_nsec;
  i64 value_sec;
  i64 value_nsec;
};

#define TFD_TIMER_ABSTIME 1
#define TFD_TIMER_CANCEL_ON_SET 2

static u64 tfd_now_ms(void) {
  return (u64)schedule_get_ticks() * 1000ull / (u64)SCHEDULE_FREQUENCY;
}

static u64 tfd_ts_to_ms(i64 sec, i64 nsec) {
  if (sec <= 0 && nsec <= 0) {
    return 0;
  }
  if (sec < 0) {
    sec = 0;
  }
  if (nsec < 0) {
    nsec = 0;
  }
  /* 纳秒向上取整到毫秒：调小会让定时器"早醒"，libinput 对早于 expiry 的
   * flush 只当没到点，等于把超时往后推一整轮。 */
  return (u64)sec * 1000ull + ((u64)nsec + 999999ull) / 1000000ull;
}

/* 把"已经过点"折算成到期次数；一次性定时器折算完自行解除武装。
 * 这个函数是幂等的：没到期什么都不做。 */
static void tfd_refresh(struct timerfd_k* t) {
  u64 now;
  if (t == NULL || t->magic != TIMERFD_MAGIC || t->next_ms == 0) {
    return;
  }
  now = tfd_now_ms();
  if (now < t->next_ms) {
    return;
  }
  if (t->interval_ms == 0) {
    t->expirations += 1;
    t->next_ms = 0; /* 一次性：到期即解除武装 */
    return;
  }
  {
    u64 n = (now - t->next_ms) / t->interval_ms + 1;
    t->expirations += n;
    t->next_ms += n * t->interval_ms;
  }
}

/* 剩余时间（毫秒，未武装为 0）：timerfd_gettime 与 settime 的 old 共用 */
static u64 tfd_remaining_ms(struct timerfd_k* t) {
  u64 now;
  if (t == NULL || t->magic != TIMERFD_MAGIC || t->next_ms == 0) {
    return 0;
  }
  now = tfd_now_ms();
  return (t->next_ms > now) ? (t->next_ms - now) : 0;
}

static int tfd_write_itimerspec(void* utmr, u64 remaining_ms, u64 interval_ms) {
  struct tfd_itimerspec its;
  if (utmr == NULL) {
    return 0;
  }
  its.interval_sec = (i64)(interval_ms / 1000ull);
  its.interval_nsec = (i64)((interval_ms % 1000ull) * 1000000ull);
  its.value_sec = (i64)(remaining_ms / 1000ull);
  its.value_nsec = (i64)((remaining_ms % 1000ull) * 1000000ull);
  if (sys_copy_to_user(utmr, &its, sizeof(its)) < 0) {
    return -14;
  }
  return 0;
}

/* 未到期 ⇒ -EAGAIN（非阻塞语义）。libinput 的 timerfd 是 TFD_NONBLOCK，
 * 内核侧不登记阻塞等待（本内核也没有 timerfd 的唤醒源）。 */
static u32 tfd_read(vnode_t* node, u32 offset, u32 size, u8* buffer) {
  struct timerfd_k* t;
  u64 v;
  (void)offset;
  if (node == NULL || node->device == NULL || buffer == NULL) {
    return 0;
  }
  t = (struct timerfd_k*)node->device;
  if (t->magic != TIMERFD_MAGIC || size < 8) {
    return 0;
  }
  tfd_refresh(t);
  if (t->expirations == 0) {
    return (u32)-11; /* -EAGAIN */
  }
  v = t->expirations;
  t->expirations = 0;
  kmemcpy(buffer, &v, 8);
  return 8;
}

static vnode_t* timerfd_make(void) {
  vnode_t* node = vfs_create_node("timerfd", 0);
  struct timerfd_k* t = kmalloc(sizeof(struct timerfd_k), KERNEL_TYPE);
  voperator_t* op = kmalloc(sizeof(voperator_t), KERNEL_TYPE);
  if (node == NULL || t == NULL || op == NULL) {
    kfree(t);
    kfree(op);
    return NULL;
  }
  kmemset(t, 0, sizeof(*t));
  t->magic = TIMERFD_MAGIC;
  node->device = t;
  node->op = op;
  op->read = tfd_read;
  op->close = evfd_node_close;
  return node;
}

int sys_timerfd_create(int clockid, int flags) {
  thread_t* current = thread_current();
  vnode_t* node;
  fd_t* fd;
  (void)flags; /* TFD_NONBLOCK/TFD_CLOEXEC：本内核 fd 无隐藏标志位 */
  /* 本内核只有"单调"和"实时"两条时基，且 timerfd 只按单调推进；
   * CLOCK_REALTIME/CLOCK_BOOTTIME 也接受（Linux 语义里它们的定时行为等价，
   * 差别只在系统时间被改动时的反应）。 */
  if (clockid != 0 /*CLOCK_REALTIME*/ && clockid != 1 /*CLOCK_MONOTONIC*/ &&
      clockid != 7 /*CLOCK_BOOTTIME*/) {
    return -22; /* -EINVAL */
  }
  node = timerfd_make();
  if (node == NULL) {
    return -12;
  }
  fd = fd_open(node, DEVICE_TYPE_VIRTUAL, "/dev/timerfd");
  if (fd == NULL) {
    return -12;
  }
  return thread_add_fd(current, fd);
}

int sys_timerfd_settime(int fd, int flags, void* utmr, void* uold) {
  struct timerfd_k* t = (struct timerfd_k*)evfd_priv(fd, TIMERFD_MAGIC);
  struct tfd_itimerspec its;
  u64 value_ms;
  u64 interval_ms;
  int rc;

  if (t == NULL) {
    return -9; /* -EBADF */
  }
  if (utmr == NULL) {
    return -22; /* -EINVAL */
  }
  if (sys_copy_from_user(&its, utmr, sizeof(its)) < 0) {
    return -14; /* -EFAULT */
  }
  /* old 是"调用前的剩余时间"，必须在改写状态之前取 */
  rc = tfd_write_itimerspec(uold, tfd_remaining_ms(t), t->interval_ms);
  if (rc < 0) {
    return rc;
  }

  value_ms = tfd_ts_to_ms(its.value_sec, its.value_nsec);
  interval_ms = tfd_ts_to_ms(its.interval_sec, its.interval_nsec);

  if (value_ms == 0) {
    t->next_ms = 0; /* 解除武装 */
    t->interval_ms = 0;
    t->expirations = 0;
    return 0;
  }

  /* 周期只对"周期性定时器"有意义：Linux 上 it_value 为 0 时 it_interval
   * 也被忽略，这里同样处理（上面已 early-return）。 */
  t->interval_ms = interval_ms;

  /* 【重新装填必须清零已到期计数】Linux 的 do_timerfd_settime 同样把
   * ctx->ticks 归零。不清零的后果：某次到期后没人 read（调用方只是重新
   * arm 同一个 timerfd，例如 libwayland 的定时器源被复用），expirations
   * 就一直 > 0 ⇒ epoll_wait 每次判它就绪并立刻返回 ⇒ 事件循环 100% 空转
   * （实测 weston 空闲时 40 秒内 550k 次 epoll_pwait）。 */
  t->expirations = 0;

  if (flags & TFD_TIMER_ABSTIME) {
    t->next_ms = value_ms;
  } else {
    t->next_ms = tfd_now_ms() + value_ms;
  }
  tfd_refresh(t); /* 绝对时刻已经过去 ⇒ 立即算一次到期 */
  return 0;
}

int sys_timerfd_gettime(int fd, void* utmr) {
  struct timerfd_k* t = (struct timerfd_k*)evfd_priv(fd, TIMERFD_MAGIC);
  if (t == NULL) {
    return -9;
  }
  if (utmr == NULL) {
    return -22;
  }
  tfd_refresh(t);
  return tfd_write_itimerspec(utmr, tfd_remaining_ms(t), t->interval_ms);
}

/* 供 sys_fd_ready_t / sys_fd_polled_t 使用：-1 表示"不是 timerfd" */
int sys_timerfd_node_ready(vnode_t* node) {
  struct timerfd_k* t;
  if (node == NULL || node->device == NULL) {
    return -1;
  }
  t = (struct timerfd_k*)node->device;
  if (t->magic != TIMERFD_MAGIC) {
    return -1;
  }
  tfd_refresh(t);
  return (t->expirations > 0) ? 1 : 0;
}
