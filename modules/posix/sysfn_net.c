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

int sys_socketpair(int domain, int type, int protocol, int sv[2]) {
  log_debug("sys_socketpair domain=%d type=%d\n", domain, type);

  // Create two connected sockets
  int s1 = sys_socket(domain, type, protocol);
  if (s1 < 0) return -1;

  int s2 = sys_socket(domain, type, protocol);
  if (s2 < 0) {
    sys_close(s1);
    return -1;
  }

  // For AF_UNIX, connect them directly
  // For now, just return the pair
  sv[0] = s1;
  sv[1] = s2;

  return 0;
}

int sys_bind(int sockfd, const struct sockaddr* addr, socklen_t addrlen) {
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

int sys_setsockopt(int sockfd, int level, int optname, const void* optval,
                   socklen_t optlen) {
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

// Network syscall registration
void sys_fn_net_init(void** syscall_table) {
  // Network syscalls
#ifdef SYS_SOCKET
  syscall_table[SYS_SOCKET] = &sys_socket;
#endif
#ifdef SYS_SOCKETPAIR
  syscall_table[SYS_SOCKETPAIR] = &sys_socketpair;
#endif
#ifdef SYS_BIND
  syscall_table[SYS_BIND] = &sys_bind;
#endif
#ifdef SYS_LISTEN
  syscall_table[SYS_LISTEN] = &sys_listen;
#endif
#ifdef SYS_ACCEPT
  syscall_table[SYS_ACCEPT] = &sys_accept;
#endif
#ifdef SYS_ACCEPT4
  syscall_table[SYS_ACCEPT4] = &sys_accept4;
#endif
#ifdef SYS_CONNECT
  syscall_table[SYS_CONNECT] = &sys_connect;
#endif
#ifdef SYS_GETSOCKNAME
  syscall_table[SYS_GETSOCKNAME] = &sys_getsockname;
#endif
#ifdef SYS_GETPEERNAME
  syscall_table[SYS_GETPEERNAME] = &sys_getpeername;
#endif
#ifdef SYS_SENDTO
  syscall_table[SYS_SENDTO] = &sys_sendto;
#endif
#ifdef SYS_RECVFROM
  syscall_table[SYS_RECVFROM] = &sys_recvfrom;
#endif
#ifdef SYS_SETSOCKOPT
  syscall_table[SYS_SETSOCKOPT] = &sys_setsockopt;
#endif
#ifdef SYS_GETSOCKOPT
  syscall_table[SYS_GETSOCKOPT] = &sys_getsockopt;
#endif
#ifdef SYS_SHUTDOWN
  syscall_table[SYS_SHUTDOWN] = &sys_shutdown;
#endif
}
