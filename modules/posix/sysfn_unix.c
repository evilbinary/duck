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

#define log_debug

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
  return len;
}

/* ===================== socket 对象 / fd ===================== */

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

/* 取 fd 对应的本机 socket（不是 AF_UNIX 的返回 NULL） */
static struct unix_sock* unix_fd_sock(int fd) {
  thread_t* current = thread_current();
  fd_t* f;
  vnode_t* node;
  struct unix_sock* sk;
  if (current == NULL) {
    return NULL;
  }
  f = thread_find_fd_id(current, fd);
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

int unix_owns(int fd) { return unix_fd_sock(fd) != NULL; }

/* ===================== 地址 / 命名空间 ===================== */

/* 从用户 sockaddr 提取"命名键"：family 2 字节 + 路径（去掉尾部 NUL 填充）。
 * 抽象名以首个 NUL 开头，去掉尾填充后仍保留首 NUL，不会和普通路径撞。 */
static int unix_key_of(const void* uaddr, u32 addrlen, u8* key, u32* keylen) {
  u8 raw[UNIX_KEY_MAX];
  u32 n;
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
  while (n > 0 && raw[2 + n - 1] == 0) {
    n--;
  }
  kmemcpy(key, raw, n + 2);
  *keylen = n + 2;
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

int unix_socket(int type, int protocol) {
  struct unix_sock* sk;
  int fd;
  (void)protocol;
  /* 去掉 SOCK_NONBLOCK(0x800) / SOCK_CLOEXEC(0x80000)：本通道恒为非阻塞，
   * close-on-exec 在当前内核无 exec 关闭语义（与 lwIP 分支一致）。 */
  type &= ~(0x800 | 0x80000);
  if (type != SOCK_STREAM && type != SOCK_DGRAM) {
    return -91; /* -EPROTONOSUPPORT */
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
  log_error("UXBIND fd=%d len=%d b0=%x b2=%x name=%s\n", fd, keylen, key[0],
            key[2], (keylen > 2) ? (char*)(key + 2) : "");
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
  log_error("UXCONN fd=%d len=%d b0=%x b2=%x name=%s -> %s\n", fd, keylen,
            key[0], key[2], (keylen > 2) ? (char*)(key + 2) : "",
            ls != NULL ? (ls->listening ? "ok" : "not-listen") : "none");
  /* 【诊断·可删】连到 X 服务器的进程，把它接下来的 syscall 全打出来 */
  {
    thread_t* c = thread_current();
    /* X11 的连接名是 /tmp/.X11-unix/X0（可能是抽象命名空间，路径从 key+3 起），
     * 用长度区间兜住两种写法 */
    if (c != NULL && keylen > 10 && keylen < 30) {
      extern void sys_probe_arm(int tid, int count);
      sys_probe_arm((int)c->id, 40);
    }
  }
  if (ls == NULL || !ls->listening) {
    return -111; /* -ECONNREFUSED：名字没有被 listen 的端点占用 */
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
      log_error("UXACCEPT tid=%d fd=%d pend=%d\n", c != NULL ? (int)c->id : -1,
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
  {
    /* 【诊断·可删】accept 之后把服务端接下来 40 个系统调用全打出来：
     * 定位"accept 成功后立刻 close 连接"到底卡在 read 还是别的调用。 */
    extern void sys_probe_arm(int tid, int count);
    thread_t* c = thread_current();
    if (c != NULL) {
      sys_probe_arm((int)c->id, 40);
    }
  }
  return nfd;
}

int unix_socketpair(int type, int protocol, int* sv) {
  struct unix_sock* a;
  struct unix_sock* b;
  int pair[2];
  int fda;
  (void)protocol;
  if (sv == NULL) {
    return -14;
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

int unix_poll_ready(int fd, int want_read, int want_write) {
  struct unix_sock* sk = unix_fd_sock(fd);
  if (sk == NULL) {
    return -1;
  }
  if (want_read) {
    if (sk->listening && sk->pend_head != NULL) {
      /* 【诊断·可删】只在"真有 pending 连接"时打印：定位服务端是否感知到
       * 客户端已连接（若打了却仍不 accept，问题在 epoll/accept 侧）。 */
      static int lp_dbg = 0;
      if (lp_dbg++ < 100) {
        thread_t* c = thread_current();
        log_error("UXREADY-PEND tid=%d fd=%d pend=%d\n",
                  c != NULL ? (int)c->id : -1, fd, (int)sk->pend_count);
      }
    }
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
