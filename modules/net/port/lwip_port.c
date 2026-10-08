#include "lwip_port.h"

#include "lwip/errno.h"
#include "lwip/sockets.h"

/* 诊断用：本文件属 lwip 端口层，不便拉入内核头，直接声明。 */
int kprintf(const char* fmt, ...);
struct lin_sin {
  unsigned short family;
  unsigned short port;
  unsigned int addr;
  unsigned char zero[8];
};

void lwip_netif_start(void);

static int lwip_err(int rc) {
  if (rc >= 0) return rc;
  return errno ? -errno : -1;
}

/* musl 的 MSG_* 和 lwIP 的编号不一样，只翻译会用到的几位。 */
static int to_lwip_flags(int flags) {
  int out = 0;
  if (flags & 0x0001) out |= MSG_OOB;
  if (flags & 0x0002) out |= MSG_PEEK;
  if (flags & 0x0040) out |= MSG_DONTWAIT;
  if (flags & 0x0100) out |= MSG_WAITALL;
  if (flags & 0x4000) out |= MSG_NOSIGNAL;
  return out;
}

static int fill_lwip_sin(struct sockaddr_in* in, const void* addr, unsigned long len) {
  const struct lin_sin* s = (const struct lin_sin*)addr;
  if (addr == NULL || len < 8) {
    errno = EINVAL;
    return -1;
  }
  memset(in, 0, sizeof(*in));
  in->sin_len = sizeof(*in);
  in->sin_family = (u8_t)s->family;
  in->sin_port = s->port;
  in->sin_addr.s_addr = s->addr;
  return 0;
}

static void export_sin(void* addr, unsigned long* addrlen, const struct sockaddr_in* in) {
  struct lin_sin* s;
  if (addr == NULL || addrlen == NULL || *addrlen < 16) return;
  s = (struct lin_sin*)addr;
  memset(s, 0, 16);
  s->family = in->sin_family;
  s->port = in->sin_port;
  s->addr = in->sin_addr.s_addr;
  *addrlen = 16;
}

/* Linux SOL_SOCKET / SO_* → lwIP。对不上的选项忽略成功，避免把应用卡死。 */
static int map_so(int opt) {
  switch (opt) {
    case 2: return SO_REUSEADDR;
    case 3: return SO_TYPE;
    case 4: return SO_ERROR;
    case 6: return SO_BROADCAST;
    case 8: return SO_RCVBUF;
    case 9: return SO_KEEPALIVE;
    case 20: return SO_RCVTIMEO;
    case 21: return SO_SNDTIMEO;
    default: return -1;
  }
}

int lwip_port_init(void) {
  lwip_netif_start();
  return 0;
}

int lwip_port_socket(int domain, int type, int protocol) {
  int s = lwip_socket(domain, type, protocol);
  if (s < 0) return lwip_err(s);
  /* 【必须非阻塞】socket 的 read/write 是在用户态系统调用上下文（中断关）
   * 里跑到 lwIP 的。若 socket 是阻塞模式，lwip_send 在 TCP 发送缓冲满时会
   * 走 netconn 的 mbox 等待 —— 而此时时钟打不进来，tcpip 线程无法推进，
   * 于是永久死锁（实测：发首帧 12 字节即卡死、shell 也卡死）。
   * 设成非阻塞后表现即"返回 EAGAIN，由用户态重试"。 */
  {
    int nb = 1;
    lwip_ioctl(s, FIONBIO, &nb);
  }
  return lwip_err(s);
}

int lwip_port_bind(int lwfd, const void* addr, unsigned long len) {
  struct sockaddr_in in;
  if (fill_lwip_sin(&in, addr, len) != 0) return -EINVAL;
  return lwip_err(lwip_bind(lwfd, (struct sockaddr*)&in, sizeof(in)));
}

int lwip_port_listen(int lwfd, int backlog) {
  return lwip_err(lwip_listen(lwfd, backlog));
}

int lwip_port_accept(int lwfd, void* addr, unsigned long* addrlen) {
  struct sockaddr_in in;
  socklen_t sl = sizeof(in);
  int fd = lwip_accept(lwfd, (struct sockaddr*)&in, &sl);
  if (fd < 0) return lwip_err(fd);
  /* 【新连接也必须非阻塞】lwip_port_socket 只对监听 socket 设了非阻塞；
   * accept 出来的连接 socket 默认仍是阻塞的。若漏设，大包 lwip_send 会在
   * syscall（中断关）里等 mbox → tcpip 线程无法推进 → 永久死锁
   * （实测：发首帧 16KB 块时卡死）。这里对新 fd 一并设非阻塞。 */
  {
    int nb = 1;
    lwip_ioctl(fd, FIONBIO, &nb);
  }
  export_sin(addr, addrlen, &in);
  return fd;
}

int lwip_port_connect(int lwfd, const void* addr, unsigned long len) {
  struct sockaddr_in in;
  if (fill_lwip_sin(&in, addr, len) != 0) return -EINVAL;
  return lwip_err(lwip_connect(lwfd, (struct sockaddr*)&in, sizeof(in)));
}

int lwip_port_shutdown(int lwfd, int how) {
  return lwip_err(lwip_shutdown(lwfd, how));
}

int lwip_port_getsockname(int lwfd, void* addr, unsigned long* addrlen) {
  struct sockaddr_in in;
  socklen_t sl = sizeof(in);
  int rc = lwip_getsockname(lwfd, (struct sockaddr*)&in, &sl);
  if (rc < 0) return lwip_err(rc);
  export_sin(addr, addrlen, &in);
  return 0;
}

int lwip_port_getpeername(int lwfd, void* addr, unsigned long* addrlen) {
  struct sockaddr_in in;
  socklen_t sl = sizeof(in);
  int rc = lwip_getpeername(lwfd, (struct sockaddr*)&in, &sl);
  if (rc < 0) return lwip_err(rc);
  export_sin(addr, addrlen, &in);
  return 0;
}

long lwip_port_sendto(int lwfd, const void* buf, unsigned long len, int flags,
                      const void* addr, unsigned long addrlen) {
  int fl = to_lwip_flags(flags);
  int n;
  static unsigned sc = 0;
  int is_big = (len > 4096);
  if (is_big && sc < 3) {
    kprintf("lw_send enter lwfd=%d len=%d\n", lwfd, (int)len);
  }
  if (addr != NULL) {
    struct sockaddr_in in;
    if (fill_lwip_sin(&in, addr, addrlen) != 0) return -EINVAL;
    n = lwip_sendto(lwfd, buf, len, fl, (struct sockaddr*)&in, sizeof(in));
  } else {
    n = lwip_send(lwfd, buf, len, fl);
  }
  if (is_big && sc < 3) {
    kprintf("lw_send ret n=%d\n", n);
    sc++;
  }
  return lwip_err(n);
}

long lwip_port_recvfrom(int lwfd, void* buf, unsigned long len, int flags,
                        void* addr, unsigned long* addrlen) {
  int fl = to_lwip_flags(flags);
  int n;
  if (addr != NULL && addrlen != NULL) {
    struct sockaddr_in in;
    socklen_t sl = sizeof(in);
    n = lwip_recvfrom(lwfd, buf, len, fl, (struct sockaddr*)&in, &sl);
    if (n < 0) return lwip_err(n);
    export_sin(addr, addrlen, &in);
    return n;
  }
  n = lwip_recv(lwfd, buf, len, fl);
  return lwip_err(n);
}

int lwip_port_setsockopt(int lwfd, int level, int opt, const void* val,
                         unsigned long len) {
  int lv = level;
  int op = opt;
  if (level == 1) {
    lv = SOL_SOCKET;
    op = map_so(opt);
    if (op < 0) return 0;
  }
  return lwip_err(lwip_setsockopt(lwfd, lv, op, val, (socklen_t)len));
}

int lwip_port_getsockopt(int lwfd, int level, int opt, void* val, unsigned long* len) {
  int lv = level;
  int op = opt;
  socklen_t sl;
  int rc;
  if (len == NULL) return -EINVAL;
  sl = (socklen_t)*len;
  if (level == 1) {
    lv = SOL_SOCKET;
    op = map_so(opt);
    if (op < 0) return 0;
  }
  rc = lwip_getsockopt(lwfd, lv, op, val, &sl);
  if (rc < 0) return lwip_err(rc);
  *len = sl;
  return 0;
}

void lwip_port_close_lwfd(int lwfd) {
  if (lwfd >= 0) lwip_close(lwfd);
}
