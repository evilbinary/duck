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
#define MEM_SIZE                        (32 * 1024)
#define MEMP_NUM_PBUF                   16
#define MEMP_NUM_UDP_PCB                4
#define MEMP_NUM_TCP_PCB                8
#define MEMP_NUM_TCP_PCB_LISTEN         4
#define MEMP_NUM_TCP_SEG                32
#define MEMP_NUM_NETBUF                 8
#define MEMP_NUM_NETCONN                8
#define MEMP_NUM_TCPIP_MSG_API          8
#define MEMP_NUM_TCPIP_MSG_INPKT        8
#define PBUF_POOL_SIZE                  16

#define TCP_MSS                         1460
#define TCP_WND                         (4 * TCP_MSS)
#define TCP_SND_BUF                     (4 * TCP_MSS)
#define TCP_SND_QUEUELEN                16
#define TCP_LISTEN_BACKLOG              1

#define TCPIP_THREAD_STACKSIZE          8192
#define TCPIP_THREAD_PRIO               1
#define TCPIP_MBOX_SIZE                 16
#define DEFAULT_RAW_RECVMBOX_SIZE       8
#define DEFAULT_UDP_RECVMBOX_SIZE       8
#define DEFAULT_TCP_RECVMBOX_SIZE       16
#define DEFAULT_ACCEPTMBOX_SIZE         8

#define LWIP_SINGLE_NETIF               1
#define LWIP_NETIF_HOSTNAME             0
#define LWIP_STATS                      0
#define LWIP_DEBUG                      0

#endif
