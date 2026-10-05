#ifndef LWIP_PORT_H
#define LWIP_PORT_H

/* 给 posix socket 系统调用用的薄封装。地址是 Linux sockaddr_in
 * （family u16、port u16、addr u32），不是 lwIP 带 sin_len 的布局。
 * 失败返回 -errno，成功返回 0、lwIP fd，或字节数。 */

int lwip_port_init(void);
int lwip_port_socket(int domain, int type, int protocol);
int lwip_port_bind(int lwfd, const void* addr, unsigned long len);
int lwip_port_listen(int lwfd, int backlog);
int lwip_port_accept(int lwfd, void* addr, unsigned long* addrlen);
int lwip_port_connect(int lwfd, const void* addr, unsigned long len);
int lwip_port_shutdown(int lwfd, int how);
int lwip_port_getsockname(int lwfd, void* addr, unsigned long* addrlen);
int lwip_port_getpeername(int lwfd, void* addr, unsigned long* addrlen);
long lwip_port_sendto(int lwfd, const void* buf, unsigned long len, int flags,
                      const void* addr, unsigned long addrlen);
long lwip_port_recvfrom(int lwfd, void* buf, unsigned long len, int flags,
                        void* addr, unsigned long* addrlen);
int lwip_port_setsockopt(int lwfd, int level, int opt, const void* val,
                         unsigned long len);
int lwip_port_getsockopt(int lwfd, int level, int opt, void* val,
                         unsigned long* len);
void lwip_port_close_lwfd(int lwfd);

#endif
