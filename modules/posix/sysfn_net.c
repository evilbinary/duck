/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "sysfn.h"

#include "kernel/devfn.h"
#include "kernel/fd.h"
#include "kernel/kernel.h"
#include "kernel/thread.h"
#include "kernel/vfs.h"
#ifdef NET_DRIVER
#include "lwip_port.h"

static int os_lwfd(int osfd) {
  thread_t* current = thread_current();
  fd_t* fd;
  /* 【诊断】非法 fd 时打印调用方返回地址+线程名，定位是谁把 -9 当 fd 用。
   * 只在 osfd<0 时打，且限流，避免 2ms 循环刷屏淹没日志。 */
  if ((u32)osfd >= 0xFFFFF000u) {
    static u32 last_ra = 0;
    u32 ra = (u32)(uintptr_t)__builtin_return_address(0);
    if (ra != last_ra) {
      last_ra = ra;
      log_error("os_lwfd bad fd %d tid %d name %s caller %x\n", osfd,
                current != NULL ? current->id : -1,
                (current != NULL && current->name != NULL) ? current->name : "?",
                ra);
    }
  }
  fd = thread_find_fd_id(current, (u32)osfd);
  if (fd == NULL) return -9; /* EBADF */
  if (fd->type != DEVICE_TYPE_NET) {
    return -88; /* ENOTSOCK */
  }
  /* 方案 A：lwip fd 记在 fd->offset（fd_open 时同步写入），快路径直接取。 */
  return (int)fd->offset;
}

/* 方案 A：socket 也走统一 vnode+op 抽象。每个连接建一个 vnode，
 * lwip fd 存 node->data，fd->data = 该 vnode（合法 vnode！）。
 * sys_read/sys_write 走 vread/vwrite → socket_operator，不再特判。 */
extern const voperator_t socket_operator;

static int lwip_os_attach(int lwfd) {
  vnode_t* node;
  fd_t* fd;
  thread_t* current;
  int id;
  if (lwfd < 0) return lwfd;
  node = vfs_create_node("socket", V_FILE);
  if (node == NULL) {
    lwip_port_close_lwfd(lwfd);
    return -12; /* ENOMEM */
  }
  node->data = (void*)(uintptr_t)(u32)lwfd;   /* 连接 id */
  node->device = device_find(DEVICE_NET);
  node->op = &socket_operator;
  fd = fd_open((u32*)node, DEVICE_TYPE_NET, "socket");
  if (fd == NULL) {
    lwip_port_close_lwfd(lwfd);
    kfree(node);
    return -12;
  }
  fd->offset = (u32)lwfd;   /* 仍记 lwip fd，os_lwfd 快路径可用 */
  current = thread_current();
  id = thread_add_fd(current, fd);
  if (id < 0) {
    fd_close(fd);
    return -12;
  }
  return id;
}
#endif

// Socket management
#define MAX_SOCKETS 64
static socket_t socket_table[MAX_SOCKETS];
static int socket_initialized;
static int socket_next_id;

static void socket_init(void) {
  if (socket_initialized) return;
  for (int i = 0; i < MAX_SOCKETS; i++) {
    socket_table[i].fd = -1;
    socket_table[i].state = SS_UNCONNECTED;
  }
  socket_initialized = 1;
}

static socket_t* socket_alloc(void) {
  socket_init();
  for (int i = 0; i < MAX_SOCKETS; i++) {
    int idx = (socket_next_id + i) % MAX_SOCKETS;
    if (socket_table[idx].fd < 0) {
      socket_table[idx].fd = idx;
      socket_table[idx].state = SS_UNCONNECTED;
      socket_table[idx].error = 0;
      socket_table[idx].data = NULL;
      socket_next_id = (idx + 1) % MAX_SOCKETS;
      return &socket_table[idx];
    }
  }
  return NULL;
}

static socket_t* socket_find(int sockfd) {
  if (sockfd < 0 || sockfd >= MAX_SOCKETS) return NULL;
  if (socket_table[sockfd].fd < 0) return NULL;
  return &socket_table[sockfd];
}

static void socket_free(socket_t* sock) {
  if (sock) {
    sock->fd = -1;
    sock->state = SS_UNCONNECTED;
    sock->data = NULL;
  }
}

int sys_socket(int domain, int type, int protocol) {
  {
    /* 【诊断·可删】所有域都打（只看业务线程 tid>=6，避开 init/tcpip 噪音） */
    static int s_dbg = 0;
    thread_t* t = thread_current();
    int tid = (t != NULL) ? (int)t->id : -1;
    if (tid >= 6 && s_dbg++ < 40) {
      log_debug("SOCK2 tid=%d dom=%d type=%d\n", tid, domain, type);
    }
  }
  /* AF_UNIX：lwIP 没有本机域，走内核内存通道（sysfn_unix.c） */
  if (domain == AF_UNIX) {
    int n = unix_socket(type, protocol);
    /* 【诊断·可删】谁在开本机 socket */
    static int s_dbg = 0;
    if (s_dbg++ < 20) {
      thread_t* t = thread_current();
      log_debug("USOCK tid=%d type=%d -> fd=%d\n",
                t != NULL ? (int)t->id : -1, type, n);
    }
    return n;
  }
#ifdef NET_DRIVER
  int lwfd = lwip_port_socket(domain, type, protocol);
  return lwip_os_attach(lwfd);
#endif
  log_debug("sys_socket domain=%d type=%d protocol=%d\n", domain, type, protocol);

  // Validate parameters
  if (domain != AF_INET && domain != AF_INET6 && domain != AF_UNIX) {
    log_error("sys_socket: unsupported domain %d\n", domain);
    return -1;  // EAFNOSUPPORT
  }
  if (type != SOCK_STREAM && type != SOCK_DGRAM && type != SOCK_RAW) {
    log_error("sys_socket: unsupported type %d\n", type);
    return -1;  // ESOCKTNOSUPPORT
  }

  // Allocate socket structure
  socket_t* sock = socket_alloc();
  if (sock == NULL) {
    log_error("sys_socket: no free socket slots\n");
    return -1;  // EMFILE
  }

  sock->domain = domain;
  sock->type = type;
  sock->protocol = protocol;
  sock->state = SS_UNCONNECTED;
  sock->recv_timeout = 0;
  sock->send_timeout = 0;
  sock->backlog = 0;
  kmemset(&sock->local_addr, 0, sizeof(sockaddr_in_t));
  kmemset(&sock->remote_addr, 0, sizeof(sockaddr_in_t));

  // For now, return socket index as fd
  // In full implementation, this would create a file descriptor
  thread_t* current = thread_current();
  fd_t* fd = fd_open(NULL, DEVICE_TYPE_NET, "socket");
  if (fd == NULL) {
    socket_free(sock);
    log_error("sys_socket: failed to create fd\n");
    return -1;
  }
  fd->data = sock;
  int fd_id = thread_add_fd(current, fd);
  sock->fd = fd_id;

  log_debug("sys_socket: created sockfd=%d\n", fd_id);
  return fd_id;
}

/* socket()/socketpair() 共用的类型标志位（musl 直接按位或进 type） */
#define SOCKPAIR_NONBLOCK 0x800
#define SOCKPAIR_CLOEXEC 0x80000

int sys_socketpair(int domain, int type, int protocol, int sv[2]) {
  int base = type & ~(SOCKPAIR_NONBLOCK | SOCKPAIR_CLOEXEC);

  /* 用户指针先校验：Linux 对空指针/未对齐指针一律 EFAULT，且不写入 */
  if (sv == NULL || (((uintptr_t)sv) & 3) != 0) {
    return -14; /* -EFAULT */
  }
  if (domain == AF_UNIX) {
    return unix_socketpair(type, protocol, sv);
  }
  /* 其余地址族：本内核只有本机（AF_UNIX）这条配对通路。这里按 Linux 的
   * errno 语义给出确定答案（调用方逐项核对错误码），而不是一律 EOPNOTSUPP。 */
  if (domain != AF_INET) {
    return -97; /* -EAFNOSUPPORT：未知地址族 */
  }
  if (base == SOCK_RAW) {
    return -93; /* -EPROTONOSUPPORT：非特权用户不能建原始套接字 */
  }
  if (base != SOCK_STREAM && base != SOCK_DGRAM) {
    return -22; /* -EINVAL：未知类型 */
  }
  /* INET 不支持 socketpair：类型与协议配套 ⇒ EOPNOTSUPP（协议本身没问题），
   * 不配套（TCP+DGRAM / ICMP+STREAM）⇒ EPROTONOSUPPORT。 */
  if (base == SOCK_DGRAM) {
    return (protocol == IPPROTO_UDP) ? -95 : -93;
  }
  return (protocol == IPPROTO_TCP) ? -95 : -93;
}

int sys_bind(int sockfd, const struct sockaddr* addr, socklen_t addrlen) {
  if (unix_owns(sockfd)) {
    return unix_bind(sockfd, (const void*)addr, (u32)addrlen);
  }
#ifdef NET_DRIVER
  int lwfd = os_lwfd(sockfd);
  if (lwfd < 0) return lwfd;
  return lwip_port_bind(lwfd, addr, addrlen);
#endif
  log_debug("sys_bind sockfd=%d\n", sockfd);

  thread_t* current = thread_current();
  fd_t* fd = thread_find_fd_id(current, sockfd);
  if (fd == NULL) {
    log_error("sys_bind: invalid sockfd %d\n", sockfd);
    return -1;  // EBADF
  }

  socket_t* sock = (socket_t*)fd->data;
  if (sock == NULL) {
    log_error("sys_bind: not a socket\n");
    return -1;  // ENOTSOCK
  }

  if (addr == NULL || addrlen < sizeof(sockaddr_in_t)) {
    log_error("sys_bind: invalid address\n");
    return -1;  // EINVAL
  }

  sockaddr_in_t* addr_in = (sockaddr_in_t*)addr;
  kmemcpy(&sock->local_addr, addr_in, sizeof(sockaddr_in_t));

  log_debug("sys_bind: bound to port %d addr %x\n", 
            (addr_in->sin_port >> 8) | ((addr_in->sin_port & 0xff) << 8),
            addr_in->sin_addr);

  return 0;
}

int sys_listen(int sockfd, int backlog) {
  if (unix_owns(sockfd)) {
    return unix_listen(sockfd, backlog);
  }
#ifdef NET_DRIVER
  int lwfd = os_lwfd(sockfd);
  if (lwfd < 0) return lwfd;
  return lwip_port_listen(lwfd, backlog);
#endif
  log_debug("sys_listen sockfd=%d backlog=%d\n", sockfd, backlog);

  thread_t* current = thread_current();
  fd_t* fd = thread_find_fd_id(current, sockfd);
  if (fd == NULL) {
    log_error("sys_listen: invalid sockfd %d\n", sockfd);
    return -1;
  }

  socket_t* sock = (socket_t*)fd->data;
  if (sock == NULL) {
    log_error("sys_listen: not a socket\n");
    return -1;
  }

  if (sock->type != SOCK_STREAM) {
    log_error("sys_listen: not a stream socket\n");
    return -1;  // EOPNOTSUPP
  }

  sock->state = SS_CONNECTING;  // Listening state
  sock->backlog = backlog > 0 ? backlog : SOMAXCONN;

  return 0;
}

int sys_accept(int sockfd, struct sockaddr* addr, socklen_t* addrlen) {
  /* O_PATH 描述符不具备任何 I/O 能力：Linux 在参数校验阶段就判 EBADF，
   * 不会走到"不是套接字"的 ENOTSOCK（tst_fd 系用例按此期望）。 */
  {
    thread_t* cur = thread_current();
    fd_t* ofd = (cur != NULL) ? thread_find_fd_id(cur, sockfd) : NULL;
    if (ofd != NULL && (ofd->flags & O_PATH) != 0) {
      return -9; /* -EBADF */
    }
  }
  if (unix_owns(sockfd)) {
    return unix_accept(sockfd, (void*)addr, (u32*)addrlen);
  }
#ifdef NET_DRIVER
  int lwfd = os_lwfd(sockfd);
  int nfd;
  unsigned long alen = 0;
  if (lwfd < 0) return lwfd;
  if (addrlen != NULL) alen = *addrlen;
  nfd = lwip_port_accept(lwfd, addr, addrlen != NULL ? &alen : NULL);
  if (addrlen != NULL) *addrlen = (socklen_t)alen;
  if (nfd < 0) return nfd;
  return lwip_os_attach(nfd);
#endif
  log_debug("sys_accept sockfd=%d\n", sockfd);

  thread_t* current = thread_current();
  fd_t* fd = thread_find_fd_id(current, sockfd);
  if (fd == NULL) {
    log_error("sys_accept: invalid sockfd %d\n", sockfd);
    return -1;
  }

  socket_t* listen_sock = (socket_t*)fd->data;
  if (listen_sock == NULL) {
    log_error("sys_accept: not a socket\n");
    return -1;
  }

  // Create new socket for the connection
  int new_fd = sys_socket(listen_sock->domain, listen_sock->type, listen_sock->protocol);
  if (new_fd < 0) return -1;

  fd_t* new_fd_struct = thread_find_fd_id(current, new_fd);
  socket_t* new_sock = (socket_t*)new_fd_struct->data;

  // In a full implementation, this would wait for an incoming connection
  // For now, return the new socket
  new_sock->state = SS_CONNECTED;

  if (addr && addrlen && *addrlen >= sizeof(sockaddr_in_t)) {
    kmemcpy(addr, &new_sock->remote_addr, sizeof(sockaddr_in_t));
    *addrlen = sizeof(sockaddr_in_t);
  }

  log_debug("sys_accept: accepted new sockfd=%d\n", new_fd);
  return new_fd;
}

int sys_accept4(int sockfd, struct sockaddr* addr, socklen_t* addrlen, int flags) {
  log_debug("sys_accept4 sockfd=%d flags=%d\n", sockfd, flags);
  // For now, just call accept
  return sys_accept(sockfd, addr, addrlen);
}

int sys_connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen) {
  {
    /* 【诊断·可删】客户端 connect 走哪条通道（unix / 网络）与地址族 */
    static int c_dbg = 0;
    if (c_dbg++ < 20) {
      thread_t* t = thread_current();
      u16 fam = (addr != NULL && addrlen >= 2)
                    ? *(volatile u16*)addr
                    : 0xffff;
      log_debug("SCONN tid=%d fd=%d unix=%d fam=%d len=%d\n",
                t != NULL ? (int)t->id : -1, sockfd, unix_owns(sockfd), (int)fam,
                (int)addrlen);
    }
  }
  if (unix_owns(sockfd)) {
    return unix_connect(sockfd, (const void*)addr, (u32)addrlen);
  }
#ifdef NET_DRIVER
  int lwfd = os_lwfd(sockfd);
  if (lwfd < 0) return lwfd;
  return lwip_port_connect(lwfd, addr, addrlen);
#endif
  log_debug("sys_connect sockfd=%d\n", sockfd);

  thread_t* current = thread_current();
  fd_t* fd = thread_find_fd_id(current, sockfd);
  if (fd == NULL) {
    log_error("sys_connect: invalid sockfd %d\n", sockfd);
    return -1;
  }

  socket_t* sock = (socket_t*)fd->data;
  if (sock == NULL) {
    log_error("sys_connect: not a socket\n");
    return -1;
  }

  if (addr == NULL || addrlen < sizeof(sockaddr_in_t)) {
    log_error("sys_connect: invalid address\n");
    return -1;
  }

  sockaddr_in_t* addr_in = (sockaddr_in_t*)addr;
  kmemcpy(&sock->remote_addr, addr_in, sizeof(sockaddr_in_t));

  // In a full implementation, this would initiate a TCP connection
  sock->state = SS_CONNECTED;

  log_debug("sys_connect: connected to port %d addr %x\n",
            (addr_in->sin_port >> 8) | ((addr_in->sin_port & 0xff) << 8),
            addr_in->sin_addr);

  return 0;
}

int sys_getsockname(int sockfd, struct sockaddr* addr, socklen_t* addrlen) {
  if (unix_owns(sockfd)) {
    return unix_getsockname(sockfd, (void*)addr, (u32*)addrlen);
  }
#ifdef NET_DRIVER
  int lwfd = os_lwfd(sockfd);
  unsigned long alen = 0;
  int rc;
  if (lwfd < 0) return lwfd;
  if (addrlen != NULL) alen = *addrlen;
  rc = lwip_port_getsockname(lwfd, addr, addrlen != NULL ? &alen : NULL);
  if (addrlen != NULL) *addrlen = (socklen_t)alen;
  return rc;
#endif
  log_debug("sys_getsockname sockfd=%d\n", sockfd);

  thread_t* current = thread_current();
  fd_t* fd = thread_find_fd_id(current, sockfd);
  if (fd == NULL) {
    log_error("sys_getsockname: invalid sockfd %d\n", sockfd);
    return -1;
  }

  socket_t* sock = (socket_t*)fd->data;
  if (sock == NULL) {
    log_error("sys_getsockname: not a socket\n");
    return -1;
  }

  if (addr && addrlen && *addrlen >= sizeof(sockaddr_in_t)) {
    kmemcpy(addr, &sock->local_addr, sizeof(sockaddr_in_t));
    *addrlen = sizeof(sockaddr_in_t);
  }

  return 0;
}

int sys_getpeername(int sockfd, struct sockaddr* addr, socklen_t* addrlen) {
  if (unix_owns(sockfd)) {
    return unix_getpeername(sockfd, (void*)addr, (u32*)addrlen);
  }
#ifdef NET_DRIVER
  int lwfd = os_lwfd(sockfd);
  unsigned long alen = 0;
  int rc;
  if (lwfd < 0) return lwfd;
  if (addrlen != NULL) alen = *addrlen;
  rc = lwip_port_getpeername(lwfd, addr, addrlen != NULL ? &alen : NULL);
  if (addrlen != NULL) *addrlen = (socklen_t)alen;
  return rc;
#endif
  log_debug("sys_getpeername sockfd=%d\n", sockfd);

  thread_t* current = thread_current();
  fd_t* fd = thread_find_fd_id(current, sockfd);
  if (fd == NULL) {
    log_error("sys_getpeername: invalid sockfd %d\n", sockfd);
    return -1;
  }

  socket_t* sock = (socket_t*)fd->data;
  if (sock == NULL) {
    log_error("sys_getpeername: not a socket\n");
    return -1;
  }

  if (sock->state != SS_CONNECTED) {
    log_error("sys_getpeername: not connected\n");
    return -1;  // ENOTCONN
  }

  if (addr && addrlen && *addrlen >= sizeof(sockaddr_in_t)) {
    kmemcpy(addr, &sock->remote_addr, sizeof(sockaddr_in_t));
    *addrlen = sizeof(sockaddr_in_t);
  }

  return 0;
}

ssize_t sys_sendto(int sockfd, const void* buf, size_t len, int flags,
                   const struct sockaddr* dest_addr, socklen_t addrlen) {
  /* 本机 socket：已连接的点对点通道，直接当 write；SOCK_DGRAM 的
   * "按地址发送"这里不支持（名字只能用来建立连接）。 */
  if (unix_owns(sockfd)) {
    (void)flags;
    (void)dest_addr;
    (void)addrlen;
    return (ssize_t)sys_write((u32)sockfd, (void*)buf, len);
  }
#ifdef NET_DRIVER
  int lwfd = os_lwfd(sockfd);
  if (lwfd < 0) return lwfd;
  return (ssize_t)lwip_port_sendto(lwfd, buf, len, flags, dest_addr, addrlen);
#endif
  log_debug("sys_sendto sockfd=%d len=%d flags=%d\n", sockfd, len, flags);

  thread_t* current = thread_current();
  fd_t* fd = thread_find_fd_id(current, sockfd);
  if (fd == NULL) {
    log_error("sys_sendto: invalid sockfd %d\n", sockfd);
    return -1;
  }

  socket_t* sock = (socket_t*)fd->data;
  if (sock == NULL) {
    log_error("sys_sendto: not a socket\n");
    return -1;
  }

  // For connectionless sockets, use dest_addr
  if (dest_addr && addrlen > 0 && sock->type == SOCK_DGRAM) {
    sockaddr_in_t* addr_in = (sockaddr_in_t*)dest_addr;
    // Store destination for UDP
    kmemcpy(&sock->remote_addr, addr_in, sizeof(sockaddr_in_t));
  }

  // In full implementation, this would call network driver
  // For now, use the network device directly
  device_t* net_dev = device_find(DEVICE_NET);
  if (net_dev && net_dev->write) {
    return net_dev->write(net_dev, buf, len);
  }

  log_debug("sys_sendto: no network driver, returning len\n");
  return len;  // Simulate success
}

ssize_t sys_recvfrom(int sockfd, void* buf, size_t len, int flags,
                     struct sockaddr* src_addr, socklen_t* addrlen) {
  {
    /* 【诊断·可删】客户端收包走哪条通道（只打业务线程，额度放宽） */
    static int r_dbg = 0;
    thread_t* t = thread_current();
    int tid = (t != NULL) ? (int)t->id : -1;
    if (tid >= 6 && r_dbg++ < 200) {
      log_debug("SRECV tid=%d fd=%d unix=%d\n", tid, sockfd, unix_owns(sockfd));
    }
  }
  if (unix_owns(sockfd)) {
    ssize_t n;
    (void)flags;
    n = (ssize_t)sys_read((u32)sockfd, buf, len);
    if (n > 0 && src_addr != NULL && addrlen != NULL) {
      (void)unix_getpeername(sockfd, (void*)src_addr, (u32*)addrlen);
    }
    return n;
  }
#ifdef NET_DRIVER
  int lwfd = os_lwfd(sockfd);
  unsigned long alen = 0;
  long n;
  if (lwfd < 0) return lwfd;
  if (addrlen != NULL) alen = *addrlen;
  n = lwip_port_recvfrom(lwfd, buf, len, flags, src_addr,
                         addrlen != NULL ? &alen : NULL);
  if (addrlen != NULL) *addrlen = (socklen_t)alen;
  return (ssize_t)n;
#endif
  log_debug("sys_recvfrom sockfd=%d len=%d flags=%d\n", sockfd, len, flags);

  thread_t* current = thread_current();
  fd_t* fd = thread_find_fd_id(current, sockfd);
  if (fd == NULL) {
    log_error("sys_recvfrom: invalid sockfd %d\n", sockfd);
    return -1;
  }

  socket_t* sock = (socket_t*)fd->data;
  if (sock == NULL) {
    log_error("sys_recvfrom: not a socket\n");
    return -1;
  }

  // In full implementation, this would call network driver
  // For now, use the network device directly
  device_t* net_dev = device_find(DEVICE_NET);
  if (net_dev && net_dev->read) {
    ssize_t ret = net_dev->read(net_dev, buf, len);
    
    // Fill source address if requested
    if (src_addr && addrlen && *addrlen >= sizeof(sockaddr_in_t) && ret > 0) {
      kmemcpy(src_addr, &sock->remote_addr, sizeof(sockaddr_in_t));
      *addrlen = sizeof(sockaddr_in_t);
    }
    
    return ret;
  }

  log_debug("sys_recvfrom: no network driver\n");
  return -1;  // Would block or error
}

/* ===================== sendmsg(211)/recvmsg(212) =====================
 * 多缓冲收发接口。最小实现：逐 iovec 走 sendto/recvfrom 同款通道；
 * msg_control（SCM_RIGHTS fd 传递）不支持——核心协议不用 fd passing。 */
struct iovec_k {
  void* base;
  size_t len;
};

struct msghdr_k {
  void* msg_name;
  socklen_t msg_namelen;
  struct iovec_k* msg_iov;
  size_t msg_iovlen;
  void* msg_control;
  size_t msg_controllen;
  int msg_flags;
};

ssize_t sys_sendmsg(int sockfd, void* u_umsg, int flags) {
  struct msghdr_k msg;
  struct iovec_k iov;
  size_t i;
  ssize_t total = 0;
  const struct msghdr_k* umsg = (const struct msghdr_k*)u_umsg;
  if (umsg == NULL) {
    return -14; /* -EFAULT */
  }
  if (sys_copy_from_user(&msg, (void*)umsg, sizeof(msg)) < 0) {
    return -14;
  }
  if (msg.msg_iov == NULL || msg.msg_iovlen == 0) {
    return 0;
  }
  if (msg.msg_iovlen > 64) {
    msg.msg_iovlen = 64;
  }
  for (i = 0; i < msg.msg_iovlen; i++) {
    if (sys_copy_from_user(&iov, (void*)((char*)msg.msg_iov + i * sizeof(iov)),
                           sizeof(iov)) < 0) {
      return total > 0 ? total : -14;
    }
    if (iov.base == NULL || iov.len == 0) {
      continue;
    }
    ssize_t n = sys_sendto(sockfd, iov.base, iov.len, flags, msg.msg_name,
                           msg.msg_namelen);
    if (n < 0) {
      return total > 0 ? total : (int)n;
    }
    total += n;
  }
  return total;
}

ssize_t sys_recvmsg(int sockfd, void* u_umsg, int flags) {
  struct msghdr_k msg;
  struct iovec_k iov;
  size_t i;
  ssize_t total = 0;
  struct msghdr_k* umsg = (struct msghdr_k*)u_umsg;
  if (umsg == NULL) {
    return -14;
  }
  if (sys_copy_from_user(&msg, umsg, sizeof(msg)) < 0) {
    return -14;
  }
  if (msg.msg_iov == NULL || msg.msg_iovlen == 0) {
    return 0;
  }
  if (msg.msg_iovlen > 64) {
    msg.msg_iovlen = 64;
  }
  for (i = 0; i < msg.msg_iovlen; i++) {
    if (sys_copy_from_user(
            &iov, (void*)((char*)msg.msg_iov + i * sizeof(iov)), sizeof(iov)) <
        0) {
      return total > 0 ? total : -14;
    }
    if (iov.base == NULL || iov.len == 0) {
      continue;
    }
    ssize_t n = sys_recvfrom(sockfd, iov.base, iov.len, flags, msg.msg_name,
                             &msg.msg_namelen);
    if (n < 0) {
      return total > 0 ? total : (int)n;
    }
    total += n;
    if ((size_t)n < iov.len) {
      break; /* 短读：剩余 iov 不再填 */
    }
  }
  /* 回写 namelen 与 flags；control 不支持（controllen 置 0） */
  msg.msg_controllen = 0;
  msg.msg_flags = 0;
  sys_copy_to_user(umsg, &msg, sizeof(msg));
  return total;
}

int sys_setsockopt(int sockfd, int level, int optname, const void* optval,
                   socklen_t optlen) {
  if (unix_owns(sockfd)) {
    return unix_setsockopt(sockfd, level, optname, (const void*)optval,
                           (u32)optlen);
  }
#ifdef NET_DRIVER
  int lwfd = os_lwfd(sockfd);
  if (lwfd < 0) return lwfd;
  return lwip_port_setsockopt(lwfd, level, optname, optval, optlen);
#endif
  log_debug("sys_setsockopt sockfd=%d level=%d optname=%d\n", sockfd, level, optname);

  thread_t* current = thread_current();
  fd_t* fd = thread_find_fd_id(current, sockfd);
  if (fd == NULL) {
    log_error("sys_setsockopt: invalid sockfd %d\n", sockfd);
    return -1;
  }

  socket_t* sock = (socket_t*)fd->data;
  if (sock == NULL) {
    log_error("sys_setsockopt: not a socket\n");
    return -1;
  }

  if (optval == NULL) {
    return -1;  // EINVAL
  }

  // Handle socket-level options
  if (level == SOL_SOCKET) {
    switch (optname) {
      case SO_RCVTIMEO:
        if (optlen >= sizeof(u32)) {
          sock->recv_timeout = *(u32*)optval;
        }
        break;
      case SO_SNDTIMEO:
        if (optlen >= sizeof(u32)) {
          sock->send_timeout = *(u32*)optval;
        }
        break;
      case SO_REUSEADDR:
      case SO_REUSEPORT:
        // Ignore for now
        break;
      default:
        log_debug("sys_setsockopt: unhandled optname %d\n", optname);
        break;
    }
  }

  return 0;
}

int sys_getsockopt(int sockfd, int level, int optname, void* optval,
                   socklen_t* optlen) {
  if (unix_owns(sockfd)) {
    return unix_getsockopt(sockfd, level, optname, optval, (u32*)optlen);
  }
#ifdef NET_DRIVER
  int lwfd = os_lwfd(sockfd);
  unsigned long ol = 0;
  int rc;
  if (lwfd < 0) return lwfd;
  if (optlen != NULL) ol = *optlen;
  rc = lwip_port_getsockopt(lwfd, level, optname, optval, optlen != NULL ? &ol : NULL);
  if (optlen != NULL) *optlen = (socklen_t)ol;
  return rc;
#endif
  log_debug("sys_getsockopt sockfd=%d level=%d optname=%d\n", sockfd, level, optname);

  thread_t* current = thread_current();
  fd_t* fd = thread_find_fd_id(current, sockfd);
  if (fd == NULL) {
    log_error("sys_getsockopt: invalid sockfd %d\n", sockfd);
    return -1;
  }

  socket_t* sock = (socket_t*)fd->data;
  if (sock == NULL) {
    log_error("sys_getsockopt: not a socket\n");
    return -1;
  }

  if (optval == NULL || optlen == NULL) {
    return -1;  // EINVAL
  }

  // Handle socket-level options
  if (level == SOL_SOCKET) {
    switch (optname) {
      case SO_TYPE:
        if (*optlen >= sizeof(int)) {
          *(int*)optval = sock->type;
          *optlen = sizeof(int);
        }
        break;
      case SO_ERROR:
        if (*optlen >= sizeof(int)) {
          *(int*)optval = sock->error;
          sock->error = 0;  // Clear error after reading
          *optlen = sizeof(int);
        }
        break;
      case SO_RCVTIMEO:
        if (*optlen >= sizeof(u32)) {
          *(u32*)optval = sock->recv_timeout;
          *optlen = sizeof(u32);
        }
        break;
      case SO_SNDTIMEO:
        if (*optlen >= sizeof(u32)) {
          *(u32*)optval = sock->send_timeout;
          *optlen = sizeof(u32);
        }
        break;
      default:
        log_debug("sys_getsockopt: unhandled optname %d\n", optname);
        break;
    }
  }

  return 0;
}

int sys_shutdown(int sockfd, int how) {
  if (unix_owns(sockfd)) {
    return unix_shutdown(sockfd, how);
  }
#ifdef NET_DRIVER
  int lwfd = os_lwfd(sockfd);
  if (lwfd < 0) return lwfd;
  return lwip_port_shutdown(lwfd, how);
#endif
  log_debug("sys_shutdown sockfd=%d how=%d\n", sockfd, how);

  thread_t* current = thread_current();
  fd_t* fd = thread_find_fd_id(current, sockfd);
  if (fd == NULL) {
    log_error("sys_shutdown: invalid sockfd %d\n", sockfd);
    return -1;
  }

  socket_t* sock = (socket_t*)fd->data;
  if (sock == NULL) {
    log_error("sys_shutdown: not a socket\n");
    return -1;
  }

  if (sock->state != SS_CONNECTED) {
    log_error("sys_shutdown: not connected\n");
    return -1;  // ENOTCONN
  }

  switch (how) {
    case SHUT_RD:
    case SHUT_WR:
    case SHUT_RDWR:
      sock->state = SS_DISCONNECTING;
      break;
    default:
      return -1;  // EINVAL
  }

  return 0;
}

/* SYS_SOCKET 这些是 sysfn_no.h 里的枚举，不是宏，#ifdef 永远为假，
 * 注册会被整段跳过，用户态 socket() 就会落到 syscall 198 not found。 */
void sys_fn_net_init(void** syscall_table) {
  syscall_table[SYS_SOCKET] = &sys_socket;
  syscall_table[SYS_SOCKETPAIR] = &sys_socketpair;
  syscall_table[SYS_BIND] = &sys_bind;
  syscall_table[SYS_LISTEN] = &sys_listen;
  syscall_table[SYS_ACCEPT] = &sys_accept;
  syscall_table[SYS_ACCEPT4] = &sys_accept4;
  syscall_table[SYS_CONNECT] = &sys_connect;
  syscall_table[SYS_GETSOCKNAME] = &sys_getsockname;
  syscall_table[SYS_GETPEERNAME] = &sys_getpeername;
  syscall_table[SYS_SENDTO] = &sys_sendto;
  syscall_table[SYS_RECVFROM] = &sys_recvfrom;
  syscall_table[SYS_SETSOCKOPT] = &sys_setsockopt;
  syscall_table[SYS_GETSOCKOPT] = &sys_getsockopt;
  syscall_table[SYS_SHUTDOWN] = &sys_shutdown;
}
