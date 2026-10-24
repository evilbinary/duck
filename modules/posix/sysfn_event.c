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
    if (fd >= 8) {
      u64 pc = (c != NULL && c->ctx != NULL && c->ctx->ic != NULL)
                   ? c->ctx->ic->pc
                   : 0;
      u64 lr = (c != NULL && c->ctx != NULL && c->ctx->ic != NULL)
                   ? c->ctx->ic->lr
                   : 0;
      log_error("EPCTL tid=%d epfd=%d op=%d fd=%d ep=%s pc=%x lr=%x\n",
                c != NULL ? (int)c->id : -1, epfd, op, fd,
                ep != NULL ? "ok" : "BAD", (u32)pc, (u32)lr);
    }
  }
  if (ep == NULL) {
    return -9; /* -EBADF */
  }
  if (op == 3 /* EPOLL_CTL_DEL */) {
    /* 【特例】Xorg 建好监听 fd 后紧跟一次 DEL 把它摘掉（ospoll 的 remove 被
     * 调到），导致主循环的 epoll 集合为空、永不 accept —— 客户端连上来只会
     * 挂在挂起队列里，X 客户端全部 "unable to open display"。
     * 其它 fd 的 DEL 照常执行（否则会留下已释放的 osfd 指针，Xorg 主线程在
     * dispatch 时拿野指针当 data.ptr 用，实测 data abort 退出）。 */
    for (i = 0; i < EPOLL_MAX_ITEMS; i++) {
      if (ep->items[i].used && ep->items[i].fd == fd) {
        if (unix_fd_is_listener(fd)) {
          log_error("EPCTL-DEL-KEEP-LISTEN fd=%d\n", fd);
          return 0;
        }
        ep->items[i].used = 0;
        ep->nitems--;
        return 0;
      }
    }
    return 2; /* -ENOENT 的绝对值 */
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
      log_error("EPEVT op=%d fd=%d size=%d w=%x,%x,%x,%x,%x,%x\n", op, fd,
                (int)sizeof(kev), (u32)raw[0], (u32)raw[1], (u32)raw[2],
                (u32)raw[3], (u32)raw[4], (u32)raw[5]);
    }
  }
  if (op == 2 /* EPOLL_CTL_MOD */) {
    for (i = 0; i < EPOLL_MAX_ITEMS; i++) {
      if (ep->items[i].used && ep->items[i].fd == fd) {
        ep->items[i].events = kev.events;
        ep->items[i].data = kev.data;
        return 0;
      }
    }
    return 2;
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
      log_error("EPWAIT-IN tid=%d epfd=%d ev=%x max=%d tmo=%d ep=%x magic=%x\n",
                c0 != NULL ? (int)c0->id : -1, epfd, (u32)(uintptr_t)events,
                maxevents, timeout, (u32)(uintptr_t)ep,
                (n0 != NULL && n0->device != NULL) ? ((u32*)n0->device)[0] : 0);
    }
  }
  int ready = 0;
  int i;
  (void)sigmask;
  (void)sigsetsize;
  if (ep == NULL) {
    return -9;
  }
  if (events == NULL || maxevents <= 0) {
    return -22;
  }
  {
    /* 【诊断·可删】wait 时把集合内容打出来：监听 fd 在不在、就绪判定 */
    static int epw_dbg = 0;
    if (epw_dbg++ < 12) {
      thread_t* c = thread_current();
      for (i = 0; i < EPOLL_MAX_ITEMS; i++) {
        if (!ep->items[i].used) {
          continue;
        }
        log_error("EPWAIT tid=%d n=%d fd=%d ev=%x listen=%d pend_r=%d\n",
                  c != NULL ? (int)c->id : -1, ep->nitems, ep->items[i].fd,
                  ep->items[i].events, unix_fd_is_listener(ep->items[i].fd),
                  sys_fd_ready(ep->items[i].fd, 1, 0));
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
  /* 无就绪：按 timeout 睡（ms）。本内核在内核态不能真阻塞（见
   * schedule_sleep 的说明），timeout<0 只能退化成轮询：
   *   - 集合为空 ⇒ 永远等不到事件，睡 100ms（10 次/秒）
   *   - 有注册项 ⇒ 睡 5ms，兼顾输入延迟与开销
   * Xorg 的 InputThread 就是"空 epoll + 无限超时"，不这样它会 5ms 一次空转。 */
  if (timeout < 0) {
    schedule_sleep(ep->nitems == 0 ? 100 : 5);
  } else if (timeout == 0) {
    schedule_sleep(1);
  } else {
    schedule_sleep(timeout > 1000 ? 1000 : timeout);
  }
  return 0;
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
