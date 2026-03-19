/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef SYSFN_H
#define SYSFN_H

#include "kernel/error.h"
#include "kernel/event.h"
#include "kernel/kernel.h"
#include "kernel/sysinfo.h"
#include "kernel/time.h"
#include "kernel/type.h"
#include "types.h"

#include "sysfn_no.h"

#define MAP_FIXED 0x10
#define MAP_ANON 0x20
#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_FAILED ((void*)-1)

#define MREMAP_MAYMOVE 1
#define MREMAP_FIXED 2
#define MREMAP_DONTUNMAP 4

#ifndef RTC_TIME
#define RTC_TIME
typedef struct rtc_time {
  u8 second;
  u8 minute;
  u8 hour;
  u8 day;
  u8 month;
  u8 year;
} rtc_time_t;
#endif

typedef struct iovec {
  void* iov_base;
  size_t iov_len;
} iovec_t;

#define _NSIG 65

typedef struct start_args {
  void* (*start_func)(void*);
  void* start_arg;
  volatile int control;
  unsigned long sig_mask[_NSIG / 8 / sizeof(long)];
} start_args_t;

typedef struct sched_param {
  int sched_priority;
  int __reserved1;
#if _REDIR_TIME64
  long __reserved2[4];
#else
  struct {
    time_t __reserved1;
    long __reserved2;
  } __reserved2[2];
#endif
  int __reserved3;
} sched_param_t;

#define PTHREAD_LIBMUSL 1
#define TLS_ABOVE_TP 1

#ifdef PTHREAD_LIBMUSL
// make libmusl happy ^_^
struct __ptcb {
  void (*__f)(void*);
  void* __x;
  struct __ptcb* __next;
};

#define LOCALE_NAME_MAX 23
struct __locale_map {
  const void* map;
  size_t map_size;
  char name[LOCALE_NAME_MAX + 1];
  const struct __locale_map* next;
};

typedef struct __locale_struct {
  const struct __locale_map* cat[6];
} locale_t;

#endif

typedef struct thread_info {
  // part 1
  struct thread_info* self;
#ifndef TLS_ABOVE_TP
  uintptr_t* dtv;
#endif
  // int reversed[2];
  struct thread_info *prev, *next; /* non-ABI */
  uintptr_t sysinfo;
#ifndef TLS_ABOVE_TP
#ifdef CANARY_PAD
  uintptr_t canary_pad;
#endif
  uintptr_t canary;
#endif

  // part 2
  u32 tid;
  u32 errno;

#ifdef PTHREAD_LIBMUSL
  volatile int detach_state;
  volatile int cancel;
  volatile unsigned char canceldisable, cancelasync;
  unsigned char tsd_used : 1;
  unsigned char dlerror_flag : 1;
  unsigned char* map_base;
  size_t map_size;
  void* stack;
  size_t stack_size;
  size_t guard_size;
  void* result;
  struct __ptcb* cancelbuf;
  void** tsd;
  struct {
    volatile void* volatile head;
    long off;
    volatile void* volatile pending;
  } robust_list;
  int h_errno_val;
  volatile int timer_id;
  locale_t* locale;
  volatile int killlock[1];
  char* dlerror_buf;
  void* stdio_locks;

  // part3
#ifdef TLS_ABOVE_TP
  uintptr_t canary;
  uintptr_t* dtv;
#endif

#endif

} thread_info_t;

#ifndef TP_OFFSET
#define TP_OFFSET 0
#endif

#ifdef TLS_ABOVE_TP
#define TP_ADJ(p) ((char*)(p) + sizeof(thread_info_t) + TP_OFFSET)
#else
#define TP_ADJ(p) (p)
#endif

enum {
  DT_EXITED = 0,
  DT_EXITING,
  DT_JOINABLE,
  DT_DETACHED,
};

#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define F_GETLK 5
#define F_SETLK 6
#define F_SETLKW 7

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_FD 2
#define FUTEX_REQUEUE 3
#define FUTEX_CMP_REQUEUE 4
#define FUTEX_WAKE_OP 5
#define FUTEX_LOCK_PI 6
#define FUTEX_UNLOCK_PI 7
#define FUTEX_TRYLOCK_PI 8
#define FUTEX_WAIT_BITSET 9

#define FUTEX_PRIVATE 128

#define FUTEX_CLOCK_REALTIME 256

#define NANOSECOND_TO_TICK(time) ((time) / (1000000000 / SCHEDULE_FREQUENCY))
#define SECOND_TO_TICK(time) ((time) / (1000 / SCHEDULE_FREQUENCY))
#define TICK_TO_NANOSECOND(tick) (tick * 1000000)

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID 3
#define CLOCK_MONOTONIC_RAW 4
#define CLOCK_REALTIME_COARSE 5
#define CLOCK_MONOTONIC_COARSE 6
#define CLOCK_BOOTTIME 7
#define CLOCK_REALTIME_ALARM 8
#define CLOCK_BOOTTIME_ALARM 9
#define CLOCK_SGI_CYCLE 10
#define CLOCK_TAI 11

// Socket address family
#define AF_UNSPEC   0
#define AF_UNIX     1
#define AF_INET     2
#define AF_INET6    10

// Socket types
#define SOCK_STREAM    1
#define SOCK_DGRAM     2
#define SOCK_RAW       3

// Socket protocol
#define IPPROTO_IP    0
#define IPPROTO_TCP   6
#define IPPROTO_UDP   17

// Socket options
#define SOL_SOCKET    1
#define SO_DEBUG      1
#define SO_REUSEADDR  2
#define SO_TYPE       3
#define SO_ERROR      4
#define SO_DONTROUTE  5
#define SO_BROADCAST  6
#define SO_SNDBUF     7
#define SO_RCVBUF     8
#define SO_KEEPALIVE  9
#define SO_OOBINLINE  10
#define SO_LINGER     13
#define SO_REUSEPORT  15
#define SO_RCVTIMEO   20
#define SO_SNDTIMEO   21
#define SO_ACCEPTCONN 30

// Message flags
#define MSG_OOB       0x0001
#define MSG_PEEK      0x0002
#define MSG_DONTROUTE 0x0004
#define MSG_DONTWAIT  0x0040
#define MSG_WAITALL   0x0100

// Shutdown flags
#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2

// Max pending connections
#define SOMAXCONN  128

// Socket state
#define SS_UNCONNECTED  0
#define SS_CONNECTING   1
#define SS_CONNECTED    2
#define SS_DISCONNECTING 3

// sockaddr structure
typedef struct sockaddr {
  u16 sa_family;
  char sa_data[14];
} sockaddr_t;

typedef u32 socklen_t;

// sockaddr_in for IPv4
typedef struct sockaddr_in {
  u16 sin_family;
  u16 sin_port;
  u32 sin_addr;
  u8 sin_zero[8];
} sockaddr_in_t;

// Socket structure (kernel internal)
typedef struct socket {
  int domain;
  int type;
  int protocol;
  int state;
  int fd;
  u32 flags;
  void *data;
  struct sockaddr_in local_addr;
  struct sockaddr_in remote_addr;
  u32 recv_timeout;
  u32 send_timeout;
  int backlog;
  int error;
} socket_t;

struct rusage {
  struct timeval ru_utime;
  struct timeval ru_stime;
  /* linux extentions, but useful */
  long ru_maxrss;
  long ru_ixrss;
  long ru_idrss;
  long ru_isrss;
  long ru_minflt;
  long ru_majflt;
  long ru_nswap;
  long ru_inblock;
  long ru_oublock;
  long ru_msgsnd;
  long ru_msgrcv;
  long ru_nsignals;
  long ru_nvcsw;
  long ru_nivcsw;
  /* room for more... */
  long __reserved[16];
};

u32 sys_open(char* name, int attr, ...);
// size_t sys_ioctl(u32 fd, u32 cmd, ...);
size_t sys_ioctl(u32 fd, u32 cmd, void* args);
int sys_close(u32 fd);
size_t sys_write(u32 fd, void* buf, size_t nbytes);
size_t sys_read(u32 fd, void* buf, size_t nbytes);
size_t sys_yeild();

int sys_print(char* s);
int sys_print_at(char* s, u32 x, u32 y);
u32 sys_exec(char* filename, char* const argv[], char* const envp[]);
void sys_test();
void sys_exit(int status);

void* sys_vmap(void* addr, size_t size);

void sys_vumap(void* ptr, size_t size);

void* sys_alloc_alignment(size_t size, u32 alignment);

void sys_free_alignment(void* ptr);
size_t sys_seek(u32 fd, size_t offset, int whence);

void* sys_valloc(void* addr, size_t size);
void sys_vfree(void* addr);

void* sys_vheap();

int sys_fork();
int sys_pipe(int fd[2]);

int sys_getpid();

int sys_getppid();

int sys_dup(int oldfd);

int sys_dup2(int oldfd, int newfd);
int sys_readdir(int fd, int index, void* dirent);
int sys_brk(u32 end);

int sys_readv(int fd, iovec_t* vector, int count);
int sys_writev(int fd, iovec_t* vector, int count);
int sys_chdir(const char* path);

void* sys_mmap2(void* addr, size_t length, int prot, int flags, int fd,
                size_t pgoffset);

int sys_mprotect(const void* start, size_t len, int prot);

int sys_rt_sigprocmask(int, void* set, void* old_set);

unsigned int sys_alarm(unsigned int seconds);

int sys_unlink(const char* pathname);
int sys_rename(const char* old, const char* new);

int sys_set_thread_area(void* set);

void sys_dumps();

int sys_getdents64(unsigned int fd, vdirent_t* dir, unsigned int count);
int sys_munmap(void* addr, size_t size);
int sys_fcntl64(int fd, int cmd, void* arg);
int sys_getcwd(char* buf, size_t size);
int sys_fchdir(int fd);

int sys_clone(int flags, void* stack, int* parent_tid, void* tls,
              int child_tid);

int sys_llseek(int fd, int offset_hi, int offset_lo, off_t* result, int whence);

int sys_umask(int mask);

int sys_stat(const char* path, struct stat* buf);
int sys_fstat(int fd, struct stat* buf);
int sys_self(void* t);

// Network syscalls
int sys_socket(int domain, int type, int protocol);
int sys_socketpair(int domain, int type, int protocol, int sv[2]);
int sys_bind(int sockfd, const struct sockaddr* addr, socklen_t addrlen);
int sys_listen(int sockfd, int backlog);
int sys_accept(int sockfd, struct sockaddr* addr, socklen_t* addrlen);
int sys_accept4(int sockfd, struct sockaddr* addr, socklen_t* addrlen, int flags);
int sys_connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen);
int sys_getsockname(int sockfd, struct sockaddr* addr, socklen_t* addrlen);
int sys_getpeername(int sockfd, struct sockaddr* addr, socklen_t* addrlen);
ssize_t sys_sendto(int sockfd, const void* buf, size_t len, int flags,
                   const struct sockaddr* dest_addr, socklen_t addrlen);
ssize_t sys_recvfrom(int sockfd, void* buf, size_t len, int flags,
                     struct sockaddr* src_addr, socklen_t* addrlen);
int sys_setsockopt(int sockfd, int level, int optname, const void* optval,
                   socklen_t optlen);
int sys_getsockopt(int sockfd, int level, int optname, void* optval,
                   socklen_t* optlen);
int sys_shutdown(int sockfd, int how);

void sys_fn_init();
void sys_fn_net_init(void** syscall_table);

#endif