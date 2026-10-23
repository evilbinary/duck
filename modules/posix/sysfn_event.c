/* 事件类 fd：eventfd2/epoll/inotify（哨兵 vnode + fd 模式） */
#include "sysfn.h"

#include "kernel/error.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "kernel/thread.h"

#define log_debug

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

/* epoll_event 用户态布局（aarch64）：{u32 events; u64 data}，data 因
 * 8 字节对齐落在 offset 8，结构 16 字节。 */
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
  if (op == 3 /* EPOLL_CTL_DEL */) {
    for (i = 0; i < EPOLL_MAX_ITEMS; i++) {
      if (ep->items[i].used && ep->items[i].fd == fd) {
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

int sys_epoll_pwait(int epfd, void* events, int maxevents, int timeout,
                    void* sigmask, size_t sigsetsize) {
  struct epoll_k* ep = (struct epoll_k*)evfd_priv(epfd, EPOLL_K_MAGIC);
  struct epoll_event_k out;
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
  for (i = 0; i < EPOLL_MAX_ITEMS && ready < maxevents; i++) {
    if (!ep->items[i].used) {
      continue;
    }
    /* 就绪判据 = 目标 fd 仍在调用者 fd 表里 */
    if (thread_find_fd_id(thread_current(), ep->items[i].fd) == NULL) {
      continue;
    }
    out.events = ep->items[i].events;
    out.pad = 0;
    out.data = ep->items[i].data;
    sys_copy_to_user((void*)((char*)events + ready * sizeof(out)), &out,
                     sizeof(out));
    ready++;
  }
  if (ready > 0) {
    return ready;
  }
  /* 无就绪：按 timeout 睡（ms；-1/0 都睡 1 tick 轮询兜底） */
  schedule_sleep(timeout > 0 ? (timeout > 1000 ? 1000 : timeout) : 1);
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
