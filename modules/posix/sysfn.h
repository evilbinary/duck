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

/* Linux clone(2) flags used by musl pthread_create. */
#define CLONE_VM 0x00000100
#define CLONE_VFORK 0x00004000
#define CLONE_FS 0x00000200
#define CLONE_FILES 0x00000400
#define CLONE_SIGHAND 0x00000800
#define CLONE_THREAD 0x00010000
#define CLONE_SETTLS 0x00080000
#define CLONE_PARENT_SETTID 0x00100000
#define CLONE_CHILD_CLEARTID 0x00200000

#define MREMAP_MAYMOVE 1
#define MREMAP_FIXED 2
#define MREMAP_DONTUNMAP 4

/* 【必须与 modules/rtc/rtc.h 的 rtc_time_t 逐字段一致（同为 u32）】
 * 两处用的是同一个 RTC_TIME 守卫宏，谁先被包含谁生效 —— 之前这里是 u8 版
 * (sizeof=6)，rtc 驱动却按自己的 u32 版 (sizeof=24) 做 `*rtc = rtc_time`
 * 整结构体赋值（rtc_read 不看 len），于是 sys_time()/get_fattime() 这种
 * 6 字节栈变量会被写掉后面 18 字节栈；而且 year 存不下 2026（截成 u8）。
 * 保持两份定义完全相同，包含顺序就不再影响行为。 */
#ifndef RTC_TIME
#define RTC_TIME
typedef struct rtc_time {
  u32 second;
  u32 minute;
  u32 hour;
  u32 day;
  u32 month;
  u32 year;
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
#define SECOND_TO_TICK(time) ((time) * SCHEDULE_FREQUENCY)
#define TICK_TO_NANOSECOND(tick) ((tick) * (1000000000 / SCHEDULE_FREQUENCY))

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
u32 sys_open_kernel(const char* path, int attr);
u32 sys_openat(int dirfd, const char* pathname, int flags, int mode);
int sys_mkdirat(int dirfd, const char* pathname, mode_t mode);
int sys_unlinkat(int dirfd, const char* pathname, int flags);
int sys_renameat(int olddirfd, const char* oldpath, int newdirfd,
                 const char* newpath);
int sys_renameat2(int olddirfd, const char* oldpath, int newdirfd,
                  const char* newpath, unsigned int flags);
int sys_linkat(int olddirfd, const char* oldpath, int newdirfd,
               const char* newpath, int flags);
int sys_utimensat(int dirfd, const char* pathname, const void* times, int flags);
int sys_newfstatat(int dirfd, const char* pathname, struct stat* stat,
                   int flags);
ssize_t sys_readlinkat(int dirfd, const char* restrict pathname,
                       char* restrict buf, size_t bufsiz);
int sys_faccessat(int dirfd, const char* pathname, int mode, int flags);
// size_t sys_ioctl(u32 fd, u32 cmd, ...);
size_t sys_ioctl(u32 fd, u32 cmd, void* args);
/* 【ABI 关键·勿改成 u64】这些 dispatch 由 sys_fn_call 经架构的 sys_call_fn
 * 函数指针调用：ARM32 = u32(*)(u32×6)，ARM64 = u64(*)(u64×6)，按「字」计参。
 * 用 u64 声明在 32 位 ARM 上会把参数槽从 6 个变成 12 个，a2 之后全读栈垃圾
 * （实测：open 的 flags 取到 r2 残留 = 路径指针 → "open attr range error"，
 * console 打不开 → dup2 失败 → shell 收不到输入、系统进 idle 像卡死）。
 * 字长类型 unsigned long 同时匹配 ARM32(u32) 与 ARM64(u64)。 */
typedef unsigned long sys_arg_t;
u64 sys_open_dispatch(sys_arg_t a0, sys_arg_t a1, sys_arg_t a2, sys_arg_t a3,
                      sys_arg_t a4, sys_arg_t a5);
u64 sys_access_dispatch(sys_arg_t a0, sys_arg_t a1, sys_arg_t a2,
                        sys_arg_t a3, sys_arg_t a4, sys_arg_t a5);
u64 sys_stat_dispatch(sys_arg_t a0, sys_arg_t a1, sys_arg_t a2, sys_arg_t a3,
                      sys_arg_t a4, sys_arg_t a5);
u64 sys_readlink_dispatch(sys_arg_t a0, sys_arg_t a1, sys_arg_t a2,
                          sys_arg_t a3, sys_arg_t a4, sys_arg_t a5);
int sys_close(u32 fd);
size_t sys_write(u32 fd, void* buf, size_t nbytes);
size_t sys_read(u32 fd, void* buf, size_t nbytes);
int sys_select(int nfds, unsigned long* readfds, unsigned long* writefds,
               unsigned long* exceptfds, struct timeval* timeout);
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
/* 【地址按字长】brk 的语义就是"堆顶地址"，用 int/u32 承接在 64 位机过 2GB
 * 会变负（实测 1GB 堆区实验：shell 起来即卡死）。参数/返回用 vaddr_t：
 * ARM64=u64、32 位平台=u32（types.h），与各架构 sys_call_fn 的按字 ABI 对齐，
 * 不会像写死 u64 那样在 ARM32 上把参数槽翻倍。 */
vaddr_t sys_brk(vaddr_t end);

int sys_readv(int fd, iovec_t* vector, int count);
int sys_writev(int fd, iovec_t* vector, int count);
int sys_chdir(const char* path);

void* sys_mmap2(void* addr, size_t length, int prot, int flags, int fd,
                size_t pgoffset);

int sys_mprotect(const void* start, size_t len, int prot);

int sys_rt_sigprocmask(int, void* set, void* old_set);
struct sigaction;
int sys_rt_sigaction(int signum, const struct sigaction* restrict act,
                     struct sigaction* restrict oldact);

unsigned int sys_alarm(unsigned int seconds);
int sys_getresuid(void* ruid, void* euid, void* suid);
int sys_getresgid(void* rgid, void* egid, void* sgid);

int sys_unlink(const char* pathname);
int sys_rename(const char* old, const char* new);

int sys_set_thread_area(void* set);

void sys_dumps();

int sys_getdents64(unsigned int fd, vdirent_t* dir, unsigned int count);
int sys_munmap(void* addr, size_t size);
int sys_fcntl64(int fd, int cmd, void* arg);
int sys_getrlimit64(int resource, void* lim);
int sys_prlimit64(int pid, int resource, void* new_limit, void* old_limit);
char* sys_getcwd(char* buf, size_t size);
int sys_fchdir(int fd);
int sys_fchownat(int dirfd, const char* pathname, unsigned int owner,
                 unsigned int group, int flags);
int sys_fchmodat(int dirfd, const char* pathname, unsigned int mode, int flags);
int sys_fchmod(int fd, unsigned int mode);
int sys_ftruncate(int fd, off_t length);
int sys_mkdir(const char* pathname, mode_t mode);
int sys_access(const char* pathname, int mode);
int sys_fstat64(int fd, struct stat* stat);
int sys_statfs64(const char* filename, struct statfs* stat);
u64 sys_open_dispatch(sys_arg_t a0, sys_arg_t a1, sys_arg_t a2, sys_arg_t a3,
                      sys_arg_t a4, sys_arg_t a5);
u64 sys_readlink_dispatch(sys_arg_t a0, sys_arg_t a1, sys_arg_t a2,
                          sys_arg_t a3, sys_arg_t a4, sys_arg_t a5);

int sys_clone(int flags, void* stack, int* parent_tid, void* tls,
              int child_tid);

int sys_llseek(int fd, int offset_hi, int offset_lo, off_t* result, int whence);

int sys_umask(int mask);
int sys_getuid();
int sys_geteuid();
int sys_getgid();
int sys_getegid();
int sys_setitimer(int which, void* new_val, void* old_val);

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

/* ---- AF_UNIX 本机传输（sysfn_unix.c）----
 * lwIP 只做 IPv4/IPv6，AF_UNIX 走这条纯内存通道；sysfn_net.c 里的 socket
 * 族入口按 domain / fd 归属分流过来。 */
int unix_socket(int type, int protocol);
int unix_owns(int fd);
int unix_bind(int fd, const void* addr, u32 addrlen);
int unix_listen(int fd, int backlog);
int unix_accept(int fd, void* addr, u32* addrlen);
int unix_connect(int fd, const void* addr, u32 addrlen);
int unix_getsockname(int fd, void* addr, u32* addrlen);
int unix_getpeername(int fd, void* addr, u32* addrlen);
int unix_shutdown(int fd, int how);
int unix_socketpair(int type, int protocol, int* sv);
int unix_setsockopt(int fd, int level, int optname, const void* optval,
                    u32 optlen);
int unix_getsockopt(int fd, int level, int optname, void* optval, u32* optlen);
/* select/poll 就绪判定：1 就绪 / 0 未就绪 / -1 不是本机 socket */
int unix_poll_ready(int fd, int want_read, int want_write);
/* 该 fd 是否为正在 listen 的 unix socket（epoll DEL 特例判断用） */
int unix_fd_is_listener(int fd);
/* 统一的 fd 就绪判定（sysfn_fs.c）：pipe/本机 socket 按真实状态，其余沿用
 * "存在即就绪"。poll/select/epoll 必须共用同一判据。 */
int sys_fd_ready(int fd, int want_read, int want_write);

void sys_fn_init();
void sys_fn_net_init(void** syscall_table);
/* Syscall number → handler (NULL if unset / out of range). */
void* sys_fn_get(int no);

/* ===== 拆分文件后的跨文件声明 =====
 * （sysfn_proc / sysfn_thread / sysfn_mm / sysfn_time / sysfn_misc / sysfn_fs） */
void sys_exit_group(int status);
int sys_gettid();
int sys_setpgid(int pid, int pgid);
int sys_getpgid(int pid);
int sys_kill(pid_t pid, int sig);
int sys_waitpid(pid_t pid, int* wstatus, int options);
long sys_wait4(pid_t pid, int* wstatus, int options, struct rusage* rusage);
int sys_set_tid_adress(void* ptr);
/* 内核侧 futex 唤醒（线程退出释放 clear_child_tid 用，sysfn_thread.c 实现） */
int futex_wake_kernel_userspace(u32* addr, u32 nwake);
int sys_pipe(int fds[2]);
int sys_thread_create(char* name, void* entry, void* data);
int sys_thread_dump();
int sys_thread_self();
void* sys_thread_addr(void* vaddr);
int sys_thread_map(int tid, u32 virt_addr, u32 phy_addr, u32 size, u32 attr);
int sys_sched_getparam(int pid, sched_param_t* param);
int sys_sched_setparam(int pid, sched_param_t* param);
int sys_sched_setscheduler(pid_t pid, int policy,
                           const struct sched_param* param);
int sys_sched_get_priority_max(int policy);
int sys_sched_get_priority_min(int policy);
int sys_sched_getaffinity(int pid, unsigned int len, unsigned long* mask);
int sys_futex(uint32_t* uaddr, int futex_op, uint32_t val,
              const struct timespec* timeout, uint32_t* uaddr2, uint32_t val3);
void* sys_mremap(void* old_address, size_t old_size, size_t new_size, int flags,
                 ...);
int sys_msync(void* addr, size_t length, int flags);
int sys_mlockall(int flags);
int sys_madvice(void* addr, size_t length, int advice);
int sys_nanosleep(struct timespec* req, struct timespec* rem);
int sys_clock_nanosleep(int clock, int flag, struct timespec* req,
                        struct timespec* rem);
int sys_clock_gettime64(clockid_t clockid, struct timespec* ts);
int sys_clock_getres(clockid_t clockid, struct timespec* res);
u32 sys_time(time_t* t);
int sys_setitimer(int which, void* new_val, void* old_val);
int sys_mem_info();
int sys_info(sysinfo_t* info);
int sys_uname(char* buf);
ssize_t sys_readlink(const char* restrict pathname, char* restrict buf,
                     size_t bufsiz);
int sys_statx(int dirfd, const char* restrict pathname, int flags,
              unsigned int mask, struct statx* restrict statxbuf);
void sys_test();
int sys_print(char* s);
int sys_print_at(char* s, u32 x, u32 y);
size_t sys_yeild();

/* ===================== 标准 POSIX 接口组（跨文件声明） ===================== */
int sys_copy_from_user(void* kbuf, const void* user, size_t size);
int sys_copy_to_user(void* user, const void* kbuf, size_t size);
int sys_setsid(void);
int sys_tgkill(int tgid, int tid, int sig);
int sys_setuid(uid_t uid);
int sys_setgid(gid_t gid);
int sys_setgroups(int size, const gid_t* list);
int sys_prctl(int option, unsigned long a2, unsigned long a3,
              unsigned long a4, unsigned long a5);
int sys_sigaltstack(const void* ss, void* old_ss);
int sys_getrandom(void* buf, size_t len, unsigned int flags);
int sys_close_range(u32 first, u32 last, unsigned int flags);
int sys_setrlimit(int resource, const void* rlim);
int sys_set_robust_list(void* head, size_t len);
int sys_rseq(void* ptr, u32 len, int flags, u32 sig);
int sys_ppoll(void* ufds, unsigned int nfds, void* tmo, void* sigmask);
ssize_t sys_sendmsg(int sockfd, void* umsg, int flags);
ssize_t sys_recvmsg(int sockfd, void* umsg, int flags);
int sys_eventfd2(u64 initval, int flags);
int sys_epoll_create1(int flags);
int sys_epoll_ctl(int epfd, int op, int fd, void* ev);
int sys_epoll_pwait2(int epfd, void* events, int maxevents, void* tmo,
                     void* sigmask, size_t sigsetsize);
int sys_epoll_pwait(int epfd, void* events, int maxevents, int timeout,
                    void* sigmask, size_t sigsetsize);
int sys_inotify_init1(int flags);
int sys_inotify_add_watch(int fd, const char* pathname, u32 mask);
int sys_inotify_rm_watch(int fd, int wd);

#endif