/* 事件类 fd：eventfd2/epoll/inotify（哨兵 vnode + fd 模式） */
#include "sysfn.h"

#include "kernel/error.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
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
  /* 【诊断·可删】fd>=8 全量打印（不设上限，重连/监听相关都在这个区间）；
   * DEL 额外打用户态 pc/lr，用来定位是谁把连接从集合里摘掉 */
  {
    thread_t* c = thread_current();
    /* 【诊断·限额】Xorg 主循环每个事件都 epoll_ctl，无条件打印会把串口打满
     * （日志刷得比串口快，关键行全被冲掉）。只留前 40 条。 */
    static int epctl_dbg = 0;
    if (fd >= 8 && epctl_dbg++ < 40) {
      u64 pc = (c != NULL && c->ctx != NULL && c->ctx->ic != NULL)
                   ? c->ctx->ic->pc
                   : 0;
      u64 lr = (c != NULL && c->ctx != NULL && c->ctx->ic != NULL)
                   ? c->ctx->ic->lr
                   : 0;
      log_debug("EPCTL tid=%d epfd=%d op=%d fd=%d ep=%s pc=%x lr=%x\n",
                c != NULL ? (int)c->id : -1, epfd, op, fd,
                ep != NULL ? "ok" : "BAD", (u32)pc, (u32)lr);
    }
  }
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
  /* 【诊断·可删】结构体布局验证：把原始 24 字节打出来看 data 到底在哪 */
  {
    static int ev_dbg = 0;
    if (ev_dbg++ < 20 && ev != NULL) {
      u32 raw[6];
      sys_copy_from_user(raw, ev, sizeof(raw));
      log_debug("EPEVT op=%d fd=%d size=%d w=%x,%x,%x,%x,%x,%x\n", op, fd,
                (int)sizeof(kev), (u32)raw[0], (u32)raw[1], (u32)raw[2],
                (u32)raw[3], (u32)raw[4], (u32)raw[5]);
    }
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
  struct epoll_k* ep;
  struct epoll_event_k out;
  /* 【诊断·可删】入口参数（含 ep 指针原值）：EPWAIT 探针此前一次都没打印，
   * 需要区分是"根本没进函数"还是"入口就 early-return"。 */
  {
    static int epw_in = 0;
    thread_t* c0 = thread_current();
    vnode_t* n0 = NULL;
    fd_t* f0 = thread_find_fd_id(c0, epfd);
    if (f0 != NULL) {
      n0 = (vnode_t*)f0->data;
    }
    ep = (struct epoll_k*)evfd_priv(epfd, EPOLL_K_MAGIC);
    if (epw_in++ < 30) {
      log_debug("EPWAIT-IN tid=%d epfd=%d ev=%x max=%d tmo=%d ep=%x magic=%x\n",
                c0 != NULL ? (int)c0->id : -1, epfd, (u32)(uintptr_t)events,
                maxevents, timeout, (u32)(uintptr_t)ep,
                (n0 != NULL && n0->device != NULL) ? ((u32*)n0->device)[0] : 0);
    }
  }
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
  {
    /* 【诊断·可删】只在真的检测到"就绪项"时打印，定位监听 fd 有没有被
     * 判成可读（若打了却仍不 accept，问题在服务端 accept 侧）。 */
    static int epw_dbg = 0;
    if (epw_dbg < 100) {
      thread_t* c = thread_current();
      for (i = 0; i < EPOLL_MAX_ITEMS; i++) {
        if (!ep->items[i].used) {
          continue;
        }
        if (sys_fd_ready(ep->items[i].fd, 1, 0)) {
          epw_dbg++;
          log_debug("EPWAIT-RDY tid=%d fd=%d ev=%x listen=%d\n",
                    c != NULL ? (int)c->id : -1, ep->items[i].fd,
                    ep->items[i].events, unix_fd_is_listener(ep->items[i].fd));
          break;
        }
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
