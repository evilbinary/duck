/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * AF_UNIX（本机）socket 传输。
 *
 * 背景：lwIP 只有 IPv4/IPv6，AF_UNIX 一进来就被 fill_lwip_sin() 判非法。
 * 而本机进程间通信（X11 的 /tmp/.X11-unix/X0 与抽象名、syslog 的
 * /dev/log、nscd 之类）全是 AF_UNIX。这里补一条纯内存的字节流通道：
 *
 *   · 命名空间只存在于内核内存里：bind 按 sockaddr 的 family+路径登记，
 *     connect 按同样规则查找，不落文件系统（路径只是名字，不需要真的
 *     存在该文件）。
 *   · 一对端点各持两个环形缓冲（本端 tx = 对端 rx），read/write 走
 *     vnode 的 op，因此 sys_read/sys_write/close 与普通 fd 完全一致。
 *   · 与 lwIP socket 一样是【非阻塞】的：没数据返回 -EAGAIN，让用户态
 *     重试（本内核不在内核态长阻塞，见 kernel/schedule.c 的说明）。
 ********************************************************************/
#include "sysfn.h"

#include "kernel/error.h"
#include "kernel/fd.h"
#include "kernel/kernel.h"
#include "kernel/thread.h"
#include "kernel/vfs.h"

// #define log_debug

#define UNIX_MAGIC 0x55017E00u
#define UNIX_RING_SIZE 16384
#define UNIX_MAX_BIND 32
#define UNIX_NAME_MAX 107 /* sun_path 108 字节，末字节留给 NUL */
#define UNIX_KEY_MAX (UNIX_NAME_MAX + 2)


/* 单向字节流 */
struct unix_ring {
  u8* buf;
  u32 cap;
  u32 head;  /* 读位置 */
  u32 count; /* 已缓存字节数 */
  u32 refs;  /* 两端各持一份引用，最后一个持有者释放 */
  /* 累计字节数（单调递增，不受环形回绕影响）：SCM_RIGHTS 的 fd 要
   * 绑定到字节流位置——Linux 语义是"recvmsg 读到该位置之后才交付 fd"。 */
  u32 wtotal; /* 已写入总字节 */
  u32 rtotal; /* 已读出总字节 */
  int writer_closed; /* 写端关闭：读空后返回 0（EOF） */
  int reader_closed; /* 读端关闭：再写返回 -EPIPE */
};

struct unix_sock {
  u32 magic;
  int type;  /* SOCK_STREAM / SOCK_DGRAM */
  int state; /* SS_* */
  int listening;
  int backlog;
  u32 keylen;             /* 绑定名（family 2 字节 + 路径） */
  u8 key[UNIX_KEY_MAX];
  u32 laddr_len;
  u8 laddr[UNIX_KEY_MAX];
  u32 raddr_len;
  u8 raddr[UNIX_KEY_MAX];
  struct unix_ring* rx;
  struct unix_ring* tx;
  struct unix_sock* peer;
  /* SO_PEERCRED：对端 pid（Xorg 用它判断本机客户端身份，拿不到就丢弃连接） */
  int owner_pid; /* 本端所属线程（bind/listen 者） */
  int peer_pid;  /* connect 时对端 pid */
  /* 监听者在 accept 前挂起的连接：单链 + 计数（backlog 限流） */
  struct unix_sock* pend_next;
  struct unix_sock* pend_head;
  struct unix_sock* pend_tail;
  int pend_count;
  /* 【SCM_RIGHTS 队列】sendmsg 附着的 fd 引用，按发送顺序 FIFO；
   * 挂在【接收端】的 unix_sock 上（数据进对端 rx，cmsg 同理）。
   * Linux 语义简化版：不按字节边界对齐，每次 recvmsg 吐一条——
   * wayland/X11 的用法（fd 随一条消息 sendmsg、下一条 recvmsg 收）足够。 */
  struct unix_cmsg* cq_head;
  struct unix_cmsg* cq_tail;
  int cq_count;
};

struct unix_bind_slot {
  int used;
  u32 keylen;
  u8 key[UNIX_KEY_MAX];
  struct unix_sock* sk;
};

static struct unix_bind_slot unix_binds[UNIX_MAX_BIND];

/* ===================== 环形缓冲 ===================== */

static struct unix_ring* unix_ring_new(void) {
  struct unix_ring* r = kmalloc(sizeof(struct unix_ring), KERNEL_TYPE);
  if (r == NULL) {
    return NULL;
  }
  kmemset(r, 0, sizeof(*r));
  r->buf = kmalloc(UNIX_RING_SIZE, KERNEL_TYPE);
  if (r->buf == NULL) {
    kfree(r);
    return NULL;
  }
  r->cap = UNIX_RING_SIZE;
  r->refs = 2; /* 连接的两端各持一份 */
  return r;
}

static void unix_ring_put_ref(struct unix_ring* r) {
  if (r == NULL) {
    return;
  }
  if (r->refs > 1) {
    r->refs--;
    return;
  }
  kfree(r->buf);
  kfree(r);
}

static u32 unix_ring_write(struct unix_ring* r, const u8* src, u32 len) {
  u32 tail;
  u32 first;
  if (r == NULL || src == NULL || len == 0) {
    return 0;
  }
  if (len > r->cap - r->count) {
    len = r->cap - r->count; /* 满了就少写，用户态按返回值续写 */
  }
  if (len == 0) {
    return 0;
  }
  tail = (r->head + r->count) % r->cap;
  first = r->cap - tail;
  if (first > len) {
    first = len;
  }
  kmemcpy(r->buf + tail, src, first);
  if (len > first) {
    kmemcpy(r->buf, src + first, len - first);
  }
  r->count += len;
  r->wtotal += len;
  return len;
}

static u32 unix_ring_read(struct unix_ring* r, u8* dst, u32 len) {
  u32 first;
  if (r == NULL || dst == NULL || len == 0 || r->count == 0) {
    return 0;
  }
  if (len > r->count) {
    len = r->count;
  }
  first = r->cap - r->head;
  if (first > len) {
    first = len;
  }
  kmemcpy(dst, r->buf + r->head, first);
  if (len > first) {
    kmemcpy(dst + first, r->buf, len - first);
  }
  r->head = (r->head + len) % r->cap;
  r->count -= len;
  r->rtotal += len;
  return len;
}

/* ===================== socket 对象 / fd ===================== */

struct unix_cmsg; /* 前置声明：SCM_RIGHTS（见文件尾） */
static void unix_cmsg_drop_all(struct unix_sock* sk);
static void unix_cmsg_drop_all_fds(struct unix_cmsg* c);
/* 读到数据后按登记交付控制消息（见文件尾，unix_op_read 调用） */
static void unix_recv_ctl_deliver(struct unix_sock* sk);

static struct unix_sock* unix_sock_new(int type) {
  struct unix_sock* sk = kmalloc(sizeof(struct unix_sock), KERNEL_TYPE);
  if (sk == NULL) {
    return NULL;
  }
  kmemset(sk, 0, sizeof(*sk));
  sk->magic = UNIX_MAGIC;
  sk->type = type;
  sk->state = SS_UNCONNECTED;
  return sk;
}

static void unix_sock_close_peer_links(struct unix_sock* sk) {
  struct unix_sock* p = sk->peer;
  if (p == NULL) {
    return;
  }
  p->peer = NULL;
  /* 【方向别搞反】sk->tx 是"我写对端读"的缓冲 ⇒ 我不再写 ⇒ writer_closed
   * （对端读空后 EOF）；sk->rx 是"对端写我读" ⇒ 我不再读 ⇒ reader_closed
   * （对端再写得到 EPIPE）。 */
  if (sk->tx != NULL) {
    sk->tx->writer_closed = 1;
  }
  if (sk->rx != NULL) {
    sk->rx->reader_closed = 1;
  }
  /* 半关闭会改变对端的可读（EOF）/可写状态，必须唤醒等在那里的线程 */
  poll_wait_notify();
}

static void unix_bind_remove(struct unix_sock* sk) {
  for (int i = 0; i < UNIX_MAX_BIND; i++) {
    if (unix_binds[i].used && unix_binds[i].sk == sk) {
      unix_binds[i].used = 0;
      unix_binds[i].sk = NULL;
    }
  }
}

static void unix_sock_free(struct unix_sock* sk) {
  if (sk == NULL || sk->magic != UNIX_MAGIC) {
    return;
  }
  unix_sock_close_peer_links(sk);
  unix_bind_remove(sk);
  /* 未交付的 SCM_RIGHTS：放掉 sendmsg 时持住的 fd 引用
   * （引用归零由 fd_close 正常走 vclose，不会泄漏 vnode/ring） */
  unix_cmsg_drop_all(sk);
  /* 监听者退出：挂起的连接没人接，随它一起释放（对端会读到 EOF） */
  {
    struct unix_sock* p = sk->pend_head;
    while (p != NULL) {
      struct unix_sock* next = p->pend_next;
      unix_sock_close_peer_links(p);
      unix_ring_put_ref(p->rx);
      unix_ring_put_ref(p->tx);
      p->magic = 0;
      kfree(p);
      p = next;
    }
    sk->pend_head = NULL;
    sk->pend_tail = NULL;
    sk->pend_count = 0;
  }
  unix_ring_put_ref(sk->rx);
  unix_ring_put_ref(sk->tx);
  sk->magic = 0;
  kfree(sk);
}

/* 把两个端点接成一对（分配两条单向缓冲） */
static int unix_pair(struct unix_sock* a, struct unix_sock* b) {
  struct unix_ring* ab = unix_ring_new(); /* a 写 → b 读 */
  struct unix_ring* ba = unix_ring_new(); /* b 写 → a 读 */
  if (ab == NULL || ba == NULL) {
    unix_ring_put_ref(ab);
    unix_ring_put_ref(ba);
    return -12;
  }
  a->tx = ab;
  a->rx = ba;
  b->tx = ba;
  b->rx = ab;
  a->peer = b;
  b->peer = a;
  a->state = SS_CONNECTED;
  b->state = SS_CONNECTED;
  return 0;
}

/* ---- vnode 操作表 ---- */

static u32 unix_op_read(vnode_t* node, u32 offset, u32 size, u8* buffer) {
  struct unix_sock* sk = (node != NULL) ? (struct unix_sock*)node->device : NULL;
  u32 n;
  (void)offset;
  if (sk == NULL || sk->magic != UNIX_MAGIC || buffer == NULL || size == 0) {
    return 0;
  }
  n = unix_ring_read(sk->rx, buffer, size);
  if (n > 0) {
    /* 【recvmsg 控制消息随数据交付】读到数据即按登记交付 SCM_RIGHTS：
     * 普通 read() 没有登记（表里查不到，开销是一次空扫）；
     * 阻塞 recvmsg 由 sys_rw_wait 的唤醒路径在等待者上下文重跑本函数，
     * 控制上下文仍在登记表里 ⇒ 唤醒后也能补上 fd（sys_recvmsg 的栈帧
     * 已被 schedule_switch 丢弃，不能指望它自己补）。 */
    unix_recv_ctl_deliver(sk);
    /* 读走数据 ⇒ 对端变可写：唤醒等 POLLOUT 的线程 */
    poll_wait_notify();
    return n;
  }
  if (sk->rx != NULL && sk->rx->writer_closed) {
    return 0; /* 对端关闭且已读空 ⇒ EOF */
  }
  return (u32)-11; /* EAGAIN：非阻塞，用户态重试 */
}

static u32 unix_op_write(vnode_t* node, u32 offset, u32 size, u8* buffer) {
  struct unix_sock* sk = (node != NULL) ? (struct unix_sock*)node->device : NULL;
  u32 n;
  (void)offset;
  if (sk == NULL || sk->magic != UNIX_MAGIC || buffer == NULL || size == 0) {
    return 0;
  }
  if (sk->peer == NULL && sk->tx == NULL) {
    return (u32)-32; /* EPIPE：没连上 */
  }
  if (sk->tx != NULL && sk->tx->reader_closed) {
    return (u32)-32; /* 对端已关闭 */
  }
  n = unix_ring_write(sk->tx, buffer, size);
  if (n == 0) {
    return (u32)-11; /* EAGAIN：缓冲满 */
  }
  /* 写进数据 ⇒ 对端变可读：这是 Xorg 服务端 accept 之后收到客户端
   * setup 请求包的关键唤醒点（不唤醒则对端 poll 永远等不到）。 */
  poll_wait_notify();
  return n;
}

static u32 unix_op_close(vnode_t* node) {
  if (node == NULL) {
    return 0;
  }
  unix_sock_free((struct unix_sock*)node->device);
  node->device = NULL;
  return 0;
}

static voperator_t unix_operator = {
    .read = unix_op_read,
    .write = unix_op_write,
    .close = unix_op_close,
};

static int unix_install(struct unix_sock* sk, const char* name) {
  thread_t* current = thread_current();
  vnode_t* node;
  fd_t* fd;
  int n;
  if (current == NULL || sk == NULL) {
    return -9;
  }
  node = vfs_create_node((char*)name, 0);
  if (node == NULL) {
    return -12;
  }
  node->device = sk;
  node->op = &unix_operator;
  fd = fd_open((u32*)node, DEVICE_TYPE_VIRTUAL, (char*)name);
  if (fd == NULL) {
    kfree(node);
    return -12;
  }
  n = thread_add_fd(current, fd);
  if (n < 0) {
    /* 先摘掉 device 再关 fd：让 close 路径别再释放 sk —— 失败时
     * sk 的所有权仍在调用方（unix_socket/accept 各自负责 discard）。 */
    node->device = NULL;
    fd_close(fd);
    return -12;
  }
  return n;
}

/* 取 fd 对应的本机 socket（不是 AF_UNIX 的返回 NULL）
 * owner 必须显式给：poll/select/epoll 的唤醒方是【别的线程】，
 * 替等待者查就绪时不能用自己的 fd 表。 */
static struct unix_sock* unix_fd_sock_t(thread_t* owner, int fd) {
  fd_t* f;
  vnode_t* node;
  struct unix_sock* sk;
  if (owner == NULL) {
    return NULL;
  }
  f = thread_find_fd_id(owner, fd);
  if (f == NULL || f->data == NULL) {
    return NULL;
  }
  node = (vnode_t*)f->data;
  sk = (struct unix_sock*)node->device;
  if (sk == NULL || sk->magic != UNIX_MAGIC) {
    return NULL;
  }
  return sk;
}

static struct unix_sock* unix_fd_sock(int fd) {
  return unix_fd_sock_t(thread_current(), fd);
}

int unix_owns(int fd) { return unix_fd_sock(fd) != NULL; }

/* ===================== 地址 / 命名空间 ===================== */

/* 从用户 sockaddr 提取"命名键"：family 2 字节 + 路径（截到名字结尾）。
 * 【必须按名字结尾归一化，不能只看尾部零】调用方给 addrlen 时常常传
 * sizeof(struct sockaddr_un)，名字之后的字节是调用方栈上的未初始化内容：
 * 服务端 bind 的键尾于是带进垃圾字节（实测 Xorg 绑 /tmp/.X11-unix/X0 时
 * 尾部多一个 '\t'，len=19），客户端用干净名字 connect 永远匹配不上 ⇒
 * XOpenDisplay 失败（"cannot open display"）。Linux 的 unix_mkname() 同样
 * 对路径名按 strlen 截断，抽象名则保留前缀 NUL 后按名字长度截断。
 * 抽象名以首个 NUL 开头，保留该前缀 NUL，不会和普通路径撞。 */
static int unix_key_of(const void* uaddr, u32 addrlen, u8* key, u32* keylen) {
  u8 raw[UNIX_KEY_MAX];
  u32 n;
  u32 i;
  if (uaddr == NULL || addrlen < 2) {
    return -22;
  }
  n = addrlen - 2;
  if (n > UNIX_NAME_MAX) {
    n = UNIX_NAME_MAX;
  }
  if (sys_copy_from_user(raw, (void*)uaddr, 2) < 0) {
    return -14;
  }
  if (n > 0 && sys_copy_from_user(raw + 2, (void*)((const u8*)uaddr + 2), n) < 0) {
    return -14;
  }
  /* 名字长度 = 从名字起点到第一个 NUL（抽象名跳过前缀 NUL 再找） */
  i = 0;
  if (n > 0 && raw[2] == 0) {
    i = 1;
  }
  while (i < n && raw[2 + i] != 0) {
    i++;
  }
  kmemcpy(key, raw, i + 2);
  *keylen = i + 2;
  return 0;
}

static struct unix_sock* unix_lookup(const u8* key, u32 keylen) {
  for (int i = 0; i < UNIX_MAX_BIND; i++) {
    if (!unix_binds[i].used || unix_binds[i].keylen != keylen) {
      continue;
    }
    if (kmemcmp(unix_binds[i].key, key, keylen) == 0) {
      return unix_binds[i].sk;
    }
  }
  return NULL;
}

static int unix_bind_add(struct unix_sock* sk, const u8* key, u32 keylen) {
  int free_slot = -1;
  for (int i = 0; i < UNIX_MAX_BIND; i++) {
    if (!unix_binds[i].used) {
      if (free_slot < 0) {
        free_slot = i;
      }
      continue;
    }
    if (unix_binds[i].keylen == keylen &&
        kmemcmp(unix_binds[i].key, key, keylen) == 0) {
      return -98; /* EADDRINUSE */
    }
  }
  if (free_slot < 0) {
    return -12;
  }
  unix_binds[free_slot].used = 1;
  unix_binds[free_slot].keylen = keylen;
  kmemcpy(unix_binds[free_slot].key, key, keylen);
  unix_binds[free_slot].sk = sk;
  return 0;
}

/* 把命名键写回用户 sockaddr（长度按调用方给的容量截断） */
static int unix_addr_out(const u8* key, u32 keylen, void* uaddr, u32* uaddrlen) {
  u8 out[UNIX_KEY_MAX];
  u32 cap = 0;
  u32 n;
  if (uaddr == NULL || uaddrlen == NULL) {
    return 0;
  }
  if (sys_copy_from_user(&cap, uaddrlen, sizeof(cap)) < 0) {
    return -14;
  }
  kmemset(out, 0, sizeof(out));
  n = (keylen > sizeof(out)) ? (u32)sizeof(out) : keylen;
  if (n > 0) {
    kmemcpy(out, key, n);
  }
  if (cap != 0 && cap < n) {
    n = cap;
  }
  if (n > 0 && sys_copy_to_user(uaddr, out, n) < 0) {
    return -14;
  }
  sys_copy_to_user(uaddrlen, &n, sizeof(n));
  return 0;
}

/* 失败路径上的清理：断开链路、还环形缓冲、释放对象 */
static void unix_discard(struct unix_sock* sk) {
  if (sk == NULL || sk->magic != UNIX_MAGIC) {
    return;
  }
  unix_sock_close_peer_links(sk);
  unix_ring_put_ref(sk->rx);
  unix_ring_put_ref(sk->tx);
  sk->magic = 0;
  kfree(sk);
}

/* ===================== syscall 实现 ===================== */

/* 定义见文件后部（socketpair 附近），socket()/socketpair() 共用 */
static void unix_fd_apply_flags(int fd, int type_flags);

int unix_socket(int type, int protocol) {
  struct unix_sock* sk;
  int fd;
  int type_flags = type & (0x800 | 0x80000);
  (void)protocol;
  /* 去掉 SOCK_NONBLOCK(0x800) / SOCK_CLOEXEC(0x80000)：本通道恒为非阻塞，
   * 标志位只记进 fd 层供 fcntl(F_GETFD/F_GETFL) 查询（exec 不关闭 fd）。 */
  type &= ~(0x800 | 0x80000);
  if (type != SOCK_STREAM && type != SOCK_DGRAM) {
    return -93; /* -EPROTONOSUPPORT（91 是 EPROTOTYPE，别混用） */
  }
  sk = unix_sock_new(type);
  if (sk == NULL) {
    return -12;
  }
  {
    thread_t* t = thread_current();
    sk->owner_pid = (t != NULL) ? (int)t->id : 0; /* 监听者 pid（SO_PEERCRED） */
  }
  fd = unix_install(sk, "/dev/unix");
  if (fd < 0) {
    unix_discard(sk);
    return fd;
  }
  unix_fd_apply_flags(fd, type_flags);
  return fd;
}

int unix_bind(int fd, const void* addr, u32 addrlen) {
  struct unix_sock* sk = unix_fd_sock(fd);
  u8 key[UNIX_KEY_MAX];
  u32 keylen = 0;
  int rc;
  if (sk == NULL) {
    return -88; /* -ENOTSOCK */
  }
  if (sk->keylen != 0) {
    return -22; /* 已经 bind 过 */
  }
  rc = unix_key_of(addr, addrlen, key, &keylen);
  if (rc < 0) {
    return rc;
  }
  /* 【诊断·可删】看服务端 bind 的名字（含抽象命名空间的 \0 前缀） */
  log_error("UXBIND fd=%d addrlen=%d len=%d b0=%x b2=%x name=%s\n", fd, addrlen,
            keylen, key[0], key[2],
            (keylen > 3 && key[2] == 0) ? (char*)(key + 3)
                                        : ((keylen > 2) ? (char*)(key + 2) : ""));
  rc = unix_bind_add(sk, key, keylen);
  if (rc < 0) {
    return rc;
  }
  kmemcpy(sk->key, key, keylen);
  sk->keylen = keylen;
  kmemcpy(sk->laddr, key, keylen);
  sk->laddr_len = keylen;
  return 0;
}

int unix_listen(int fd, int backlog) {
  struct unix_sock* sk = unix_fd_sock(fd);
  if (sk == NULL) {
    return -88;
  }
  if (sk->type != SOCK_STREAM) {
    return -95; /* -EOPNOTSUPP */
  }
  if (sk->keylen == 0) {
    /* Linux 会自动分配抽象名；这里要求显式 bind（X11 等都会先 bind） */
    return -22;
  }
  sk->listening = 1;
  sk->state = SS_CONNECTING;
  sk->backlog = (backlog > 0) ? backlog : 1;
  return 0;
}

int unix_connect(int fd, const void* addr, u32 addrlen) {
  struct unix_sock* sk = unix_fd_sock(fd);
  struct unix_sock* ls;
  struct unix_sock* srv;
  u8 key[UNIX_KEY_MAX];
  u32 keylen = 0;
  int rc;
  if (sk == NULL) {
    return -88;
  }
  if (sk->peer != NULL) {
    return -106; /* -EISCONN */
  }
  rc = unix_key_of(addr, addrlen, key, &keylen);
  if (rc < 0) {
    return rc;
  }
  ls = unix_lookup(key, keylen);
  /* 【诊断·可删】客户端 connect 用的名字与查表结果 */
  log_debug("UXCONN fd=%d addrlen=%d len=%d b0=%x b2=%x name=%s -> %s\n", fd,
            addrlen, keylen, key[0], key[2],
            (keylen > 3 && key[2] == 0) ? (char*)(key + 3)
                                        : ((keylen > 2) ? (char*)(key + 2) : ""),
            ls != NULL ? (ls->listening ? "ok" : "not-listen") : "none");
  /* 【探针已关】此前连到 X 的进程会被 arm 40 条 syscall 打印，但 Xorg 的
   * 主循环会反复连接/重连，日志刷得比串口快，把 UXCONN 等关键行冲掉。
   * 需要看某个进程的调用序列时，改由 kernel/syscall.c 里的 sys_probe_arm
   * 在具体位置显式打开。 */
  if (ls == NULL || !ls->listening) {
    return -111; /* -ECONNREFUSED：名字没有被 listen 的端点占用 */
  }
  /* 【写权限】路径名套接字要先过文件权限这一关：本内核没有真实权限位，
   * 用 chmod 记录表里该路径最近一次设置的模式判断（没有记录 ⇒ 放行）。
   * 抽象命名空间（key[2] == '\0'）没有文件，直接跳过。 */
  {
    char spath[128];
    u32 n = (keylen > 2) ? (keylen - 2) : 0;
    u32 mode = 0;
    if (n > sizeof(spath) - 1) {
      n = sizeof(spath) - 1;
    }
    kmemcpy(spath, key + 2, n);
    spath[n] = '\0';
    if (spath[0] != '\0' && sys_chmod_query(spath, &mode) == 0 &&
        (mode & 0200) == 0) {
      return -13; /* -EACCES：套接字文件不可写 */
    }
  }
  if (ls->type != sk->type) {
    return -91; /* -EPROTOTYPE：类型不匹配（数据报连到流式端点） */
  }
  if (ls->pend_count >= ls->backlog) {
    return -11; /* 队列满 */
  }
  srv = unix_sock_new(sk->type);
  if (srv == NULL) {
    return -12;
  }
  rc = unix_pair(sk, srv);
  if (rc < 0) {
    srv->magic = 0;
    kfree(srv);
    return rc;
  }
  kmemcpy(sk->raddr, key, keylen);
  sk->raddr_len = keylen;
  kmemcpy(srv->laddr, key, keylen);
  srv->laddr_len = keylen;
  /* SO_PEERCRED 双向 pid：服务端看到客户端 pid，客户端看到监听者 pid */
  {
    thread_t* t = thread_current();
    srv->owner_pid = ls->owner_pid;
    srv->peer_pid = (t != NULL) ? (int)t->id : 0;
    sk->peer_pid = ls->owner_pid;
  }
  if (sk->laddr_len > 0) {
    kmemcpy(srv->raddr, sk->laddr, sk->laddr_len);
    srv->raddr_len = sk->laddr_len;
  }
  /* 挂到监听者的"待 accept"队列（accept 时再变成 fd） */
  srv->pend_next = NULL;
  if (ls->pend_tail != NULL) {
    ls->pend_tail->pend_next = srv;
  } else {
    ls->pend_head = srv;
  }
  ls->pend_tail = srv;
  ls->pend_count++;
  /* 【accept 的唤醒点】服务端（Xorg）此刻正挂在 poll/epoll 上等监听 fd，
   * 这里不唤醒它就会一直睡到超时 —— 客户端则卡在握手第一阶段。 */
  poll_wait_notify();
  return 0;
}

int unix_accept(int fd, void* addr, u32* addrlen) {
  struct unix_sock* sk = unix_fd_sock(fd);
  struct unix_sock* srv;
  int nfd;
  if (sk == NULL) {
    return -88;
  }
  if (!sk->listening) {
    return -22;
  }
  srv = sk->pend_head;
  {
    /* 【诊断·可删】accept 到底有没有被服务端调到 */
    static int acc_dbg = 0;
    if (acc_dbg++ < 20) {
      thread_t* c = thread_current();
      log_debug("UXACCEPT tid=%d fd=%d pend=%d\n", c != NULL ? (int)c->id : -1,
                fd, (int)sk->pend_count);
    }
  }
  if (srv == NULL) {
    return -11; /* 暂无连接：非阻塞，用户态重试 */
  }
  sk->pend_head = srv->pend_next;
  if (sk->pend_head == NULL) {
    sk->pend_tail = NULL;
  }
  srv->pend_next = NULL;
  if (sk->pend_count > 0) {
    sk->pend_count--;
  }
  nfd = unix_install(srv, "/dev/unix");
  if (nfd < 0) {
    unix_discard(srv); /* 对端会读到 EOF */
    return nfd;
  }
  if (addr != NULL && addrlen != NULL && srv->raddr_len > 0) {
    unix_addr_out(srv->raddr, srv->raddr_len, addr, addrlen);
  }
  return nfd;
}

/* 把 socket()/socketpair() 类型里带的标志位落到 fd 层（F_GETFD/F_GETFL 可查） */
static void unix_fd_apply_flags(int fd, int type_flags) {
  thread_t* current = thread_current();
  fd_t* f;
  if (current == NULL || fd < 0) {
    return;
  }
  f = thread_find_fd_id(current, (u32)fd);
  if (f == NULL) {
    return;
  }
  if ((type_flags & 0x80000) != 0) {
    f->flags |= FD_CLOEXEC;
  }
  if ((type_flags & 0x800) != 0) {
    f->flags |= O_NONBLOCK;
  }
}

int unix_socketpair(int type, int protocol, int* sv) {
  struct unix_sock* a;
  struct unix_sock* b;
  int pair[2];
  int fda;
  int type_flags = type & (0x800 | 0x80000);
  (void)protocol;
  if (sv == NULL) {
    return -14;
  }
  type &= ~(0x800 | 0x80000);
  if (type != SOCK_STREAM && type != SOCK_DGRAM) {
    return -22; /* -EINVAL：本机套接字只支持流/数据报 */
  }
  a = unix_sock_new(type);
  b = unix_sock_new(type);
  if (a == NULL || b == NULL) {
    if (a != NULL) {
      a->magic = 0;
      kfree(a);
    }
    if (b != NULL) {
      b->magic = 0;
      kfree(b);
    }
    return -12;
  }
  if (unix_pair(a, b) < 0) {
    a->magic = 0;
    b->magic = 0;
    kfree(a);
    kfree(b);
    return -12;
  }
  fda = unix_install(a, "/dev/unixpair");
  if (fda < 0) {
    unix_discard(a);
    unix_discard(b);
    return fda;
  }
  pair[0] = fda;
  pair[1] = unix_install(b, "/dev/unixpair");
  if (pair[1] < 0) {
    /* 关掉已装好的那一端：close 会把两条环一起还掉，b 随 peer 断开失效 */
    thread_t* current = thread_current();
    fd_t* f = thread_find_fd_id(current, fda);
    if (f != NULL) {
      fd_close(f);
    }
    unix_discard(b);
    return pair[1];
  }
  sys_copy_to_user(sv, pair, sizeof(pair));
  unix_fd_apply_flags(pair[0], type_flags);
  unix_fd_apply_flags(pair[1], type_flags);
  return 0;
}

int unix_getsockname(int fd, void* addr, u32* addrlen) {
  struct unix_sock* sk = unix_fd_sock(fd);
  if (sk == NULL) {
    return -88;
  }
  return unix_addr_out(sk->laddr, sk->laddr_len, addr, addrlen);
}

int unix_getpeername(int fd, void* addr, u32* addrlen) {
  struct unix_sock* sk = unix_fd_sock(fd);
  if (sk == NULL) {
    return -88;
  }
  if (sk->peer == NULL) {
    return -107; /* -ENOTCONN */
  }
  return unix_addr_out(sk->raddr, sk->raddr_len, addr, addrlen);
}

int unix_shutdown(int fd, int how) {
  struct unix_sock* sk = unix_fd_sock(fd);
  if (sk == NULL) {
    return -88;
  }
  if (how != 0 && how != 1 && how != 2) {
    return -22;
  }
  if (how != 1) { /* 不再读 */
    if (sk->rx != NULL) {
      sk->rx->reader_closed = 1;
    }
  }
  if (how != 0) { /* 不再写：对端读空后 EOF */
    if (sk->tx != NULL) {
      sk->tx->writer_closed = 1;
    }
  }
  poll_wait_notify();
  return 0;
}

int unix_setsockopt(int fd, int level, int optname, const void* optval,
                    u32 optlen) {
  (void)level;
  (void)optname;
  (void)optval;
  (void)optlen;
  if (unix_fd_sock(fd) == NULL) {
    return -88;
  }
  return 0; /* 选项全部接受（本机通道没有发送窗口/超时概念） */
}

int unix_getsockopt(int fd, int level, int optname, void* optval,
                    u32* optlen) {
  struct unix_sock* sk = unix_fd_sock(fd);
  int v = 0;
  u32 cap = 0;
  (void)level;
  if (sk == NULL) {
    return -88;
  }
  if (optname == 17 /* SO_PEERCRED：struct ucred {pid, uid, gid} 12 字节 */) {
    struct {
      int pid;
      u32 uid;
      u32 gid;
    } cred;
    if (optval == NULL || optlen == NULL) {
      return -22;
    }
    if (sys_copy_from_user(&cap, optlen, sizeof(cap)) < 0) {
      return -14;
    }
    cred.pid = sk->peer_pid;
    cred.uid = 0;
    cred.gid = 0;
    if (cap > sizeof(cred)) {
      cap = sizeof(cred);
    }
    if (sys_copy_to_user(optval, &cred, cap) < 0) {
      return -14;
    }
    sys_copy_to_user(optlen, &cap, sizeof(cap));
    return 0;
  }
  switch (optname) {
    case 3: /* SO_TYPE */
      v = sk->type;
      break;
    case 4: /* SO_ERROR */
      v = 0;
      break;
    case 7: /* SO_SNDBUF */
    case 8: /* SO_RCVBUF */
      v = UNIX_RING_SIZE;
      break;
    default:
      v = 0;
      break;
  }
  if (optval == NULL || optlen == NULL) {
    return -22;
  }
  if (sys_copy_from_user(&cap, optlen, sizeof(cap)) < 0) {
    return -14;
  }
  if (cap > sizeof(v)) {
    cap = sizeof(v);
  }
  if (sys_copy_to_user(optval, &v, cap) < 0) {
    return -14;
  }
  sys_copy_to_user(optlen, &cap, sizeof(cap));
  return 0;
}

/* select/poll 的就绪判定（-1 表示该 fd 不是本机 socket）。
 * 读就绪：监听者有挂起连接 ‖ 收到数据 ‖ 对端关闭(EOF)；
 * 写就绪：对端在且缓冲没满。 */
/* 该 fd 是不是一个正在 listen 的本机 unix socket（epoll 的 DEL 特例用） */
int unix_fd_is_listener(int fd) {
  struct unix_sock* sk = unix_fd_sock(fd);
  return (sk != NULL && sk->listening) ? 1 : 0;
}

int unix_poll_ready_t(thread_t* owner, int fd, int want_read, int want_write) {
  struct unix_sock* sk = unix_fd_sock_t(owner, fd);
  if (sk == NULL) {
    return -1;
  }
  if (want_read) {
    if (sk->listening && sk->pend_head != NULL) {
      return 1;
    }
    if (sk->rx != NULL && (sk->rx->count > 0 || sk->rx->writer_closed)) {
      return 1;
    }
  }
  if (want_write) {
    if (sk->tx == NULL) {
      return 1; /* 还没连上：写不会阻塞，立刻返回错误 */
    }
    if (!sk->tx->reader_closed && sk->tx->count < sk->tx->cap) {
      return 1;
    }
  }
  return 0;
}

int unix_poll_ready(int fd, int want_read, int want_write) {
  return unix_poll_ready_t(thread_current(), fd, want_read, want_write);
}

/* ===================== SCM_RIGHTS fd 传递 =====================
 * wayland/X11 的命脉：客户端把 shm/pipe 的 fd 连同一条消息 sendmsg 给
 * 合成器。实现与 Linux 的差异（够用即可，见 struct unix_sock 注释）：
 *   · cmsg 挂在接收端 socket 的 FIFO 队列，不按字节边界对齐；
 *   · 每次 recvmsg（读到数据时）交付一条 cmsg；
 *   · fd 引用在 send 时 +1 持住（发送方先 close 也不丢），
 *     recv 时注入接收者 fd 表，socket 销毁时未交付的放回引用。
 * 用户态布局（aarch64 musl）：struct cmsghdr = {u64 len; i32 level;
 * i32 type} 16 字节，数据紧跟其后；SCM_RIGHTS 数据是 int fd 数组。 */
#define UNIX_CMSG_FD_MAX 16
#define UNIX_CMSG_MAX 32 /* 每端挂起 cmsg 上限，防内存被打爆 */

struct unix_cmsg {
  struct unix_cmsg* next;
  int nfds;
  /* 该 fd 附着的字节流位置（发送方这一笔 sendmsg 的起始字节号）。
   * 接收方只有读到 rtotal >= pos 时才交付——与 Linux 的 fd 语义一致：
   * 提前交付会被调用方当作"本条消息的 fd"，随后被清理丢弃。 */
  u32 pos;
  fd_t* fd[UNIX_CMSG_FD_MAX];
};

/* 放掉单条 cmsg 持住的 fd 引用（发送校验失败 / 注入失败路径用） */
static void unix_cmsg_drop_all_fds(struct unix_cmsg* c) {
  for (int i = 0; i < c->nfds; i++) {
    if (c->fd[i] != NULL) {
      fd_close(c->fd[i]);
      c->fd[i] = NULL;
    }
  }
  c->nfds = 0;
}

struct cmsghdr_k {
  u64 cmsg_len;
  int cmsg_level;
  int cmsg_type;
};
#define UNIX_SOL_SOCKET 1
#define UNIX_SCM_RIGHTS 1

static void unix_cmsg_drop_all(struct unix_sock* sk) {
  struct unix_cmsg* c = sk->cq_head;
  while (c != NULL) {
    struct unix_cmsg* next = c->next;
    for (int i = 0; i < c->nfds; i++) {
      fd_close(c->fd[i]); /* 放掉 send 时持住的引用 */
    }
    kfree(c);
    c = next;
  }
  sk->cq_head = NULL;
  sk->cq_tail = NULL;
  sk->cq_count = 0;
}

static void unix_cmsg_enqueue(struct unix_sock* sk, struct unix_cmsg* c) {
  c->next = NULL;
  if (sk->cq_tail != NULL) {
    sk->cq_tail->next = c;
  } else {
    sk->cq_head = c;
  }
  sk->cq_tail = c;
  sk->cq_count++;
}

static struct unix_cmsg* unix_cmsg_dequeue(struct unix_sock* sk) {
  struct unix_cmsg* c = sk->cq_head;
  if (c == NULL) {
    return NULL;
  }
  sk->cq_head = c->next;
  if (sk->cq_head == NULL) {
    sk->cq_tail = NULL;
  }
  sk->cq_count--;
  c->next = NULL;
  return c;
}

/* 统计一条待发 cmsg 链的节点数（commit 整链入队时累加计数用） */
static int sk_count_chain(struct unix_cmsg* c) {
  int n = 0;
  for (; c != NULL; c = c->next) {
    n++;
  }
  return n;
}

/* 【三段式收发协议·消灭竞态】sys_sendmsg 不能"先写数据再入队 cmsg"：
 * sys_sendto 里的 poll_wait_notify() 会唤醒对端，对端若在两步之间被
 * 调度上 CPU，recvmsg 读到数据但 cmsg 队列还是空的 ⇒ fd 永久丢失
 * （实测间发复现）。正确顺序：
 *   1) prepare：解析控制缓冲、持住 fd 引用，构建待发链（对端还没醒）；
 *   2) commit：把链入队【然后才写数据】——对端被唤醒时 cmsg 必已入队；
 *   3) rollback：仅当 send 整体失败（数据没进 ring）时摘链回滚，
 *      防止重试发送把同一组 fd 二次入队。
 * 与 Linux"先校验控制缓冲再发送"语义一致（EBADF 等错误时数据不发）。 */
void* unix_rights_prepare(int fd, const void* control, u32 clen, int* err) {
  thread_t* current = thread_current();
  struct unix_sock* sk = unix_fd_sock_t(current, fd);
  struct unix_cmsg* head = NULL;
  struct unix_cmsg* tail = NULL;
  u32 offset = 0;
  int count = 0;
  *err = 0;
  if (sk == NULL) {
    *err = 1; /* 不是 AF_UNIX socket：调用方按老路径忽略 control */
    return NULL;
  }
  if (control == NULL || clen == 0) {
    return NULL;
  }
  if (sk->peer == NULL) {
    *err = -107; /* -ENOTCONN */
    return NULL;
  }
  /* 本笔 sendmsg 的数据从 tx 环当前末尾开始写 ⇒ 节点构造时记 tx->wtotal
   * 作为 fd 的附着字节位置（见下方 c->pos） */
  while (offset + sizeof(struct cmsghdr_k) <= clen) {
    struct cmsghdr_k hdr;
    struct unix_cmsg* c = NULL;
    u32 len;
    int nfds;
    if (sys_copy_from_user(&hdr, (void*)((char*)control + offset),
                           sizeof(hdr)) < 0) {
      *err = -14; /* -EFAULT */
      goto fail;
    }
    len = (u32)hdr.cmsg_len;
    if (len < sizeof(hdr) || offset + len > clen) {
      break; /* 控制缓冲损坏：按 Linux 语义截断收尾 */
    }
    if (hdr.cmsg_level == UNIX_SOL_SOCKET && hdr.cmsg_type == UNIX_SCM_RIGHTS) {
      nfds = (int)((len - sizeof(hdr)) / sizeof(int));
      if (nfds > UNIX_CMSG_FD_MAX) {
        nfds = UNIX_CMSG_FD_MAX; /* 单条超出截断（正常用法 1-2 个 fd） */
      }
      if (sk->peer->cq_count + count >= UNIX_CMSG_MAX) {
        *err = -105; /* -ENOBUFS */
        goto fail;
      }
      c = kmalloc(sizeof(struct unix_cmsg), KERNEL_TYPE);
      if (c == NULL) {
        *err = -12; /* -ENOMEM */
        goto fail;
      }
      kmemset(c, 0, sizeof(*c));
      c->nfds = nfds;
      c->pos = (sk->tx != NULL) ? sk->tx->wtotal : 0;
      for (int i = 0; i < nfds; i++) {
        int ufd;
        fd_t* f;
        if (sys_copy_from_user(&ufd,
                               (void*)((char*)control + offset +
                                       sizeof(hdr) + i * sizeof(int)),
                               sizeof(int)) < 0) {
          *err = -14;
          goto fail_one;
        }
        f = thread_find_fd_id(current, ufd);
        if (f == NULL) {
          *err = -9; /* -EBADF */
          goto fail_one;
        }
        f->use_count++; /* 持引用：发送方 close 不影响在途传递 */
        c->fd[i] = f;
        continue;
      fail_one:
        unix_cmsg_drop_all_fds(c);
        kfree(c);
        goto fail;
      }
      /* 先挂本发送链（commit 时整链入队） */
      c->next = NULL;
      if (tail != NULL) {
        tail->next = c;
      } else {
        head = c;
      }
      tail = c;
      count++;
    }
    /* 下一条 cmsg：CMSG_ALIGN(8) 对齐步进 */
    offset += (len + 7u) & ~7u;
  }
  return head;
fail:
  /* 链上已持住的引用全部放掉 */
  while (head != NULL) {
    struct unix_cmsg* next = head->next;
    unix_cmsg_drop_all_fds(head);
    kfree(head);
    head = next;
  }
  return NULL;
}

void unix_rights_commit(int fd, void* staged) {
  thread_t* current = thread_current();
  struct unix_sock* sk = unix_fd_sock_t(current, fd);
  struct unix_cmsg* c = (struct unix_cmsg*)staged;
  struct unix_cmsg* last;
  if (c == NULL || sk == NULL || sk->peer == NULL) {
    unix_rights_rollback(fd, staged);
    return;
  }
  /* 整链追加到对端（接收方）队列尾。必须在数据写进 ring 之前完成：
   * 对端被 poll_wait_notify 唤醒跑 recvmsg 时，cmsg 必已在队列 */
  for (last = c; last->next != NULL; last = last->next) {
  }
  if (sk->peer->cq_tail != NULL) {
    sk->peer->cq_tail->next = c;
  } else {
    sk->peer->cq_head = c;
  }
  sk->peer->cq_tail = last;
  sk->peer->cq_count += sk_count_chain(c);
}

/* 【回滚】send 整体失败（EAGAIN/EPIPE，数据没进 ring）时把刚 commit 的链
 * 从对端队列尾摘回来并放引用——否则重试发送会二次入队，fd 被交付两次。
 * 仅当 total==0（没有任何数据写出）时调用：部分成功时 cmsg 必须随已进
 * ring 的数据交付，与 Linux 的字节附着语义一致。 */
void unix_rights_rollback(int fd, void* staged) {
  thread_t* current = thread_current();
  struct unix_sock* sk = unix_fd_sock_t(current, fd);
  struct unix_cmsg* c = (struct unix_cmsg*)staged;
  int n;
  if (c == NULL) {
    return;
  }
  n = sk_count_chain(c);
  if (sk != NULL && sk->peer != NULL) {
    /* 摘队列尾的 n 个节点（commit 时整链追加在尾部） */
    struct unix_cmsg* p = sk->peer->cq_head;
    if (p == c) {
      sk->peer->cq_head = NULL;
      sk->peer->cq_tail = NULL;
      sk->peer->cq_count = 0;
    } else {
      while (p != NULL && p->next != c) {
        p = p->next;
      }
      if (p != NULL) {
        p->next = NULL;
        sk->peer->cq_tail = p;
        sk->peer->cq_count -= n;
      }
    }
  }
  while (c != NULL) {
    struct unix_cmsg* next = c->next;
    unix_cmsg_drop_all_fds(c);
    kfree(c);
    c = next;
  }
}

/* 【显式 owner 版本】唤醒路径（poll_wait_notify 的重查）在写者上下文执行：
 * thread_current() 是写者，但 fd 表与控制缓冲属于等待者。必须由调用方
 * 显式传 owner，页表已由 poll_wait 机制切到等待者，用户内存可写。 */
int unix_recv_rights_t(thread_t* owner, int fd, void* control, u32 clen) {
  struct unix_sock* sk = unix_fd_sock_t(owner, fd);
  struct cmsghdr_k hdr;
  u32 used = 0;
  u32 consumed;
  if (sk == NULL || sk->rx == NULL || control == NULL) {
    return 0; /* 非 AF_UNIX / 无控制缓冲：无控制消息 */
  }
  /* 【按字节位置交付】只交付"本次 recvmsg 已读过的字节范围"覆盖到的 cmsg。
   * 提前交付会被调用方当成别的消息的 fd 并在清理时丢弃（wayland 实测：
   * create_pool 的 fd 在前一段数据到达时就交付了，真正用到时已被清空）。 */
  consumed = sk->rx->rtotal;
  while (sk->cq_head != NULL && sk->cq_head->pos <= consumed) {
    struct unix_cmsg* c = sk->cq_head;
    u32 payload = (u32)c->nfds * sizeof(int);
    u32 need = (u32)sizeof(hdr) + payload;
    u32 step = (need + 7u) & ~7u; /* cmsg 之间按 CMSG_ALIGN(8) 排布 */
    int outfd[UNIX_CMSG_FD_MAX];
    int i;
    if (used + need > clen) {
      break; /* 控制缓冲不够：留在队列里等下一次（不丢 fd） */
    }
    c = unix_cmsg_dequeue(sk);
    if (c == NULL) {
      break;
    }
    /* fd 注入接收者 fd 表（thread_add_fd 内部 use_count++，交接引用） */
    for (i = 0; i < c->nfds; i++) {
      int n = thread_add_fd(owner, c->fd[i]);
      if (n < 0) {
        fd_close(c->fd[i]); /* 表满：归还 send 时持的引用 */
        outfd[i] = -1;
      } else {
        outfd[i] = n;
      }
    }
    hdr.cmsg_len = need;
    hdr.cmsg_level = UNIX_SOL_SOCKET;
    hdr.cmsg_type = UNIX_SCM_RIGHTS;
    if (sys_copy_to_user((void*)((char*)control + used), &hdr,
                         sizeof(hdr)) < 0 ||
        sys_copy_to_user((void*)((char*)control + used + sizeof(hdr)), outfd,
                         payload) < 0) {
      /* 用户指针坏：注入已发生，fd 交给接收者自行关闭 */
      kfree(c);
      return used > 0 ? (int)used : -14;
    }
    used += step;
    kfree(c);
  }
  return (int)used;
}

/* 兼容包装：普通路径（当前线程就是接收者） */
int unix_recv_rights(int fd, void* control, u32 clen) {
  return unix_recv_rights_t(thread_current(), fd, control, clen);
}

/* ===================== 阻塞 recvmsg 的控制消息交付 =====================
 * 背景（实测根因）：unix socket 的 sys_read 空读会走 sys_rw_wait 挂起，
 * schedule_switch 丢弃内核调用链 —— 唤醒后线程直达用户态，sys_recvmsg
 * 的后半段（取 cmsg + 回写 msghdr）永远不执行。若读者先于写者阻塞，
 * 数据到了、fd 却永远交付不出去（控制缓冲保持用户 memset 的全零）。
 * 方案：sys_recvmsg 进入时把控制上下文登记到本表；unix_op_read 读到数据
 * 时按登记交付（阻塞场景下 vread 由 poll_wait_suspend 的重查在等待者
 * 上下文重跑，登记仍在 ⇒ 唤醒路径也能交付）。普通 read() 查不到登记，
 * 开销只是一次空扫。 */
#define UNIX_RECV_REG_MAX 8
struct unix_recv_reg {
  int used;
  int tid;
  int fd;
  void* uctl;
  u32 clen;
  void* uhdr; /* 用户 msghdr：交付后回写 controllen */
  int delivered;
  int dlen; /* 已交付的控制消息字节数（唤醒路径交付后 sys_recvmsg 取用） */
};
static struct unix_recv_reg s_recv_regs[UNIX_RECV_REG_MAX];

void unix_recv_ctl_begin(int fd, void* uctl, u32 clen, void* uhdr) {
  thread_t* current = thread_current();
  int i;
  int slot = -1;
  if (current == NULL || uctl == NULL || clen == 0) {
    return;
  }
  /* 【先按 tid 复用】阻塞唤醒后 end() 不会执行（栈帧已丢弃），登记会以
   * delivered=1 残留；同一线程下一次 recvmsg 必须复用它，否则槽位耗尽 */
  for (i = 0; i < UNIX_RECV_REG_MAX; i++) {
    if (s_recv_regs[i].used && s_recv_regs[i].tid == (int)current->id) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    for (i = 0; i < UNIX_RECV_REG_MAX; i++) {
      if (!s_recv_regs[i].used) {
        slot = i;
        break;
      }
    }
  }
  if (slot < 0) {
    return;
  }
  s_recv_regs[slot].used = 1;
  s_recv_regs[slot].tid = (int)current->id;
  s_recv_regs[slot].fd = fd;
  s_recv_regs[slot].uctl = uctl;
  s_recv_regs[slot].clen = clen;
  s_recv_regs[slot].uhdr = uhdr;
  s_recv_regs[slot].delivered = 0;
  s_recv_regs[slot].dlen = 0;
}

int unix_recv_ctl_end(void) {
  thread_t* current = thread_current();
  int i;
  if (current == NULL) {
    return 0;
  }
  for (i = 0; i < UNIX_RECV_REG_MAX; i++) {
    if (s_recv_regs[i].used && s_recv_regs[i].tid == (int)current->id) {
      /* 返回【已交付的控制字节数】：唤醒路径交付时已把 controllen 写进
       * 用户 msghdr，sys_recvmsg 必须原样沿用，不能拿自己的 0 覆盖
       * （实测：覆盖后 wayland 的 decode_cmsg 看到 controllen=0，
       * fd 明明注入了却报 "file descriptor expected"）。 */
      int d = s_recv_regs[i].delivered ? s_recv_regs[i].dlen : 0;
      s_recv_regs[i].used = 0;
      s_recv_regs[i].uhdr = NULL;
      s_recv_regs[i].dlen = 0;
      return d;
    }
  }
  return 0;
}

static void unix_recv_ctl_deliver(struct unix_sock* sk) {
  int i;
  int r;
  if (sk == NULL || sk->cq_head == NULL) {
    return; /* 无挂起 cmsg：交回 sys_recvmsg 的正常路径处理 */
  }
  /* 【不能按 thread_current 匹配】poll_wait_notify 的重查在写者上下文跑
   * （只切了页表），当前线程是写者；要用登记里的 tid 反查等待者。
   * 同一个 socket 上可能有多个等待者，逐个尝试。 */
  for (i = 0; i < UNIX_RECV_REG_MAX; i++) {
    struct unix_recv_reg* reg = &s_recv_regs[i];
    thread_t* owner;
    if (!reg->used || reg->delivered) {
      continue;
    }
    owner = thread_find_id(reg->tid);
    if (owner == NULL) {
      continue;
    }
    if (unix_fd_sock_t(owner, reg->fd) != sk) {
      continue; /* 登记的不是本次读的 socket */
    }
    r = unix_recv_rights_t(owner, reg->fd, reg->uctl, reg->clen);
    if (r <= 0) {
      continue;
    }
    reg->delivered = 1;
    reg->dlen = r;
    /* 【必须在这里回写 msghdr】阻塞唤醒后 sys_recvmsg 的写回代码不执行 */
    if (reg->uhdr != NULL) {
      struct msghdr_k hdr;
      if (sys_copy_from_user(&hdr, reg->uhdr, sizeof(hdr)) >= 0) {
        hdr.msg_controllen = (size_t)r;
        hdr.msg_flags = 0;
        sys_copy_to_user(reg->uhdr, &hdr, sizeof(hdr));
      }
    }
    return;
  }
}
