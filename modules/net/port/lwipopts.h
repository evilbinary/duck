#ifndef LWIPOPTS_H
#define LWIPOPTS_H

/* IPv4 + TCP/UDP，给用户态 socket 用。IPv6 / DHCP / DNS 先不开。 */
#define NO_SYS                          0
#define SYS_LIGHTWEIGHT_PROT            1
#define LWIP_TCPIP_CORE_LOCKING         1
#define LWIP_TCPIP_CORE_LOCKING_INPUT   1

#define LWIP_IPV4                       1
#define LWIP_IPV6                       0
#define LWIP_ARP                        1
#define LWIP_ETHERNET                   1
#define LWIP_ICMP                       1
#define LWIP_RAW                        1
#define LWIP_UDP                        1
#define LWIP_TCP                        1
#define LWIP_DNS                        0
#define LWIP_DHCP                       0
#define LWIP_AUTOIP                     0
#define LWIP_IGMP                       0

#define LWIP_NETCONN                    1
#define LWIP_SOCKET                     1
#define LWIP_NETIF_API                  0
#define LWIP_COMPAT_SOCKETS             0
#define LWIP_POSIX_SOCKETS_IO_NAMES     0
#define LWIP_SOCKET_POLL                0
#define LWIP_SOCKET_SELECT              0
#define LWIP_PROVIDE_ERRNO              1
#define SO_REUSE                        1
#define LWIP_SO_RCVTIMEO                1
#define LWIP_SO_SNDTIMEO                1

#define MEM_ALIGNMENT                   8
#define MEM_SIZE                        (128 * 1024)
#define MEMP_NUM_PBUF                   32
#define MEMP_NUM_UDP_PCB                4
#define MEMP_NUM_TCP_PCB                8
#define MEMP_NUM_TCP_PCB_LISTEN         4
#define MEMP_NUM_TCP_SEG                128
#define MEMP_NUM_NETBUF                 8
#define MEMP_NUM_NETCONN                8
#define MEMP_NUM_TCPIP_MSG_API          8
#define MEMP_NUM_TCPIP_MSG_INPKT        8
#define PBUF_POOL_SIZE                  64

#define TCP_MSS                         1460
/* 【窗口/发送缓冲取中等值】16*MSS(~23KB) 在大块连续发送时依赖对端 ACK 及时
 * 回填窗口，实测连发若干帧后会出现"窗口卡死不回填、EAGAIN 无限重试"。这里
 * 收到 8*MSS(~11.7KB)：单次仍能吞一个 16KB 块的大部分，窗口回填压力更小。 */
#define TCP_WND                         (8 * TCP_MSS)
#define TCP_SND_BUF                     (8 * TCP_MSS)
#define TCP_SND_QUEUELEN                32
#define TCP_LISTEN_BACKLOG              1

/* 16K：TCP 输入路径（含 loopif 回灌）在 tcpip 线程上跑 protothreads，
 * 8K 在大包/多连接时会贴着边。 */
#define TCPIP_THREAD_STACKSIZE          16384
#define TCPIP_THREAD_PRIO               1
#define TCPIP_MBOX_SIZE                 16
#define DEFAULT_RAW_RECVMBOX_SIZE       8
#define DEFAULT_UDP_RECVMBOX_SIZE       8
#define DEFAULT_TCP_RECVMBOX_SIZE       16
#define DEFAULT_ACCEPTMBOX_SIZE         8

/* 多网卡：loopif(127.0.0.1) + 以太网。LWIP_HAVE_LOOPIF 的成立条件是
 * LWIP_NETIF_LOOPBACK && !LWIP_SINGLE_NETIF，两个都必须改。
 * loopif 由 lwip_init()->netif_init() 自动创建（lwip/src/core/netif.c），
 * 用 tcpip_input 投递到 tcpip 线程（异步，不会在调用者栈上递归）。 */
#define LWIP_SINGLE_NETIF               0
#define LWIP_NETIF_LOOPBACK             1
/* 回环队列上限（0=不限，可能吃满 pbuf 池），给个有限值 */
#define LWIP_LOOPBACK_MAX_PBUFS         16
#define LWIP_NETIF_HOSTNAME             0
#define LWIP_STATS                      0
#define LWIP_DEBUG                      0

#endif
