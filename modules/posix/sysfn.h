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
#include "types.h"

#if defined(ARM)
enum {
  SYS_EXIT = 1,
  SYS_FORK = 2,
  SYS_READ = 3,
  SYS_WRITE = 4,
  SYS_OPEN = 5,
  SYS_CLOSE = 6,
  SYS_UNLINK = 10,
  SYS_EXEC = 11,
  SYS_CHDIR = 12,
  SYS_SEEK = 19,
  SYS_GETPID = 20,
  SYS_ALARM = 27,
  SYS_ACESS = 33,
  SYS_KILL = 37,
  SYS_RENAME = 38,
  SYS_MKDIR = 39,
  SYS_PIPE = 42,
  SYS_DUP = 41,
  SYS_BRK = 45,
  SYS_SBRK = 46,  //?
  SYS_IOCTL = 54,
  SYS_UMASK = 60,
  SYS_DUP2 = 63,
  SYS_GETPPID = 64,
  SYS_READLINK = 85,
  SYS_READDIR = 89,
  SYS_MUNMAP = 91,
  SYS_STAT = 106,
  SYS_FSTAT = 108,
  SYS_SYSINFO = 116,
  SYS_CLONE = 120,
  SYS_MPROTECT = 125,
  SYS_FCHDIR = 133,
  SYS_LLSEEK = 140,
  SYS_GETDENTS = 141,
  SYS_READV = 145,
  SYS_WRITEV = 146,
  SYS_SCHED_GETPARAM = 155,
  SYS_SCHED_SETPARAM = 156,
  SYS_SCHED_SETSCHEDULER = 157,
  SYS_SCHED_YIELD = 158,
  SYS_SCHED_GET_PRIORITY_MAX = 159,
  SYS_SCHED_GET_PRIORITY_MIN = 160,
  SYS_YIELD = 158,
  SYS_NANOSLEEP = 162,
  SYS_MREMAP = 163,
  SYS_RT_SIGACTION = 174,
  SYS_RT_SIGPROCMASK = 175,
  SYS_GETCWD = 183,
  SYS_MMAP2 = 192,
  SYS_FSTAT64 = 197,
  SYS_MADVISE = 220,
  SYS_FCNT64 = 221,
  SYS_GETTID = 224,
  SYS_GETDENTS64 = 217,  // diff from x86
  SYS_CLOCK_NANOSLEEP = 230,
  SYS_FUTEX = 240,
  SYS_SET_THREAD_AREA = 243,
  SYS_EXIT_GROUP = 248,  // diff from x86
  SYS_SET_TID_ADDRESS = 256,
  SYS_STATFS64 = 266,
  SYS_STATX = 397,
  SYS_CLOCK_GETTIME64 = 403,
  SYS_PRINT = 500,
  SYS_PRINT_AT = 501,
  SYS_DEV_READ = 502,
  SYS_DEV_WRITE = 503,
  SYS_DEV_IOCTL = 504,
  SYS_TEST = 505,
  SYS_MAP = 506,
  SYS_UMAP = 507,
  SYS_VALLOC = 508,
  SYS_VFREE = 509,
  SYS_VHEAP = 510,
  SYS_DUMPS = 511,
  SYS_SELF = 512,
  SYS_MEMINFO = 513,
  SYS_THREAD_SELF = 514,
  SYS_THREAD_CREATE = 515,
  SYS_THREAD_DUMP = 516,
  SYS_THREAD_ADDR = 517,
  SYS_THREAD_MAP = 518,
};

#elif defined(X86)

enum {
  SYS_EXIT = 1,
  SYS_FORK = 2,
  SYS_READ = 3,
  SYS_WRITE = 4,
  SYS_OPEN = 5,
  SYS_CLOSE = 6,
  SYS_UNLINK = 10,
  SYS_EXEC = 11,
  SYS_CHDIR = 12,
  SYS_SEEK = 19,
  SYS_GETPID = 20,
  SYS_ALARM = 27,
  SYS_ACESS = 33,
  SYS_KILL = 37,
  SYS_RENAME = 38,
  SYS_MKDIR = 39,
  SYS_PIPE = 42,
  SYS_DUP = 41,
  SYS_BRK = 45,
  SYS_SBRK = 46,  //?
  SYS_IOCTL = 54,
  SYS_UMASK = 60,
  SYS_DUP2 = 63,
  SYS_GETPPID = 64,
  SYS_READLINK = 85,
  SYS_READDIR = 89,
  SYS_MUNMAP = 91,
  SYS_STAT = 106,
  SYS_FSTAT = 108,
  SYS_SYSINFO = 116,
  SYS_CLONE = 120,
  SYS_MPROTECT = 125,
  SYS_FCHDIR = 133,
  SYS_LLSEEK = 140,
  SYS_GETDENTS = 141,
  SYS_READV = 145,
  SYS_WRITEV = 146,
  SYS_SCHED_GETPARAM = 155,
  SYS_SCHED_SETPARAM = 156,
  SYS_SCHED_SETSCHEDULER = 157,
  SYS_SCHED_YIELD = 158,
  SYS_SCHED_GET_PRIORITY_MAX = 159,
  SYS_SCHED_GET_PRIORITY_MIN = 160,

  SYS_YIELD = 158,
  SYS_NANOSLEEP = 162,
  SYS_MREMAP = 163,
  SYS_RT_SIGACTION = 174,
  SYS_RT_SIGPROCMASK = 175,
  SYS_GETCWD = 183,
  SYS_MMAP2 = 192,
  SYS_FSTAT64 = 197,
  SYS_GETTID = 224,
  SYS_GETDENT64 = 217,
  SYS_FCNT64 = 221,
  SYS_MADVISE = 219,
  SYS_GETDENTS64 = 220,
  SYS_CLOCK_NANOSLEEP = 230,
  SYS_FUTEX = 240,
  SYS_SET_THREAD_AREA = 243,
  SYS_EXIT_GROUP = 252,  // diff from arm
  SYS_SET_TID_ADDRESS = 258,
  SYS_STATFS64 = 266,
  SYS_STATX = 383,
  SYS_CLOCK_GETTIME64 = 403,
  SYS_PRINT = 500,
  SYS_PRINT_AT = 501,
  SYS_DEV_READ = 502,
  SYS_DEV_WRITE = 503,
  SYS_DEV_IOCTL = 504,
  SYS_TEST = 505,
  SYS_MAP = 506,
  SYS_UMAP = 507,
  SYS_VALLOC = 508,
  SYS_VFREE = 509,
  SYS_VHEAP = 510,
  SYS_DUMPS = 511,
  SYS_SELF = 512,
  SYS_MEMINFO = 513,
  SYS_THREAD_SELF = 514,
  SYS_THREAD_CREATE = 515,
  SYS_THREAD_DUMP = 516,
  SYS_THREAD_ADDR = 517,
  SYS_THREAD_MAP = 518,
};

#else
enum {
  SYS_EXIT = 1,
  SYS_FORK = 2,
  SYS_READ = 3,
  SYS_WRITE = 4,
  SYS_OPEN = 5,
  SYS_CLOSE = 6,
  SYS_UNLINK = 10,
  SYS_EXEC = 11,
  SYS_CHDIR = 12,
  SYS_SEEK = 19,
  SYS_GETPID = 20,
  SYS_ALARM = 27,
  SYS_ACESS = 33,
  SYS_KILL = 37,
  SYS_RENAME = 38,
  SYS_MKDIR = 39,
  SYS_PIPE = 42,
  SYS_DUP = 41,
  SYS_BRK = 45,
  SYS_IOCTL = 54,
  SYS_UMASK = 60,
  SYS_DUP2 = 63,
  SYS_GETPPID = 64,
  SYS_READLINK = 85,
  SYS_READDIR = 89,
  SYS_STAT = 106,
  SYS_FSTAT = 108,
  SYS_SYSINFO = 116,
  SYS_CLONE = 120,
  SYS_FCHDIR = 133,
  SYS_LLSEEK = 140,
  SYS_WRITEV = 146,
  SYS_READV = 145,
  SYS_YIELD = 158,
  SYS_SCHED_GETPARAM = 155,
  SYS_SCHED_SETPARAM = 156,
  SYS_SCHED_SETSCHEDULER = 157,
  SYS_SCHED_YIELD = 158,
  SYS_SCHED_GET_PRIORITY_MAX = 159,
  SYS_SCHED_GET_PRIORITY_MIN = 160,
  SYS_GETCWD = 183,
  SYS_MMAP2 = 192,
  SYS_FSTAT64 = 197,
  SYS_MPROTECT = 125,
  SYS_NANOSLEEP = 162,
  SYS_MREMAP = 163,
  SYS_RT_SIGACTION = 174,
  SYS_RT_SIGPROCMASK = 175,
  SYS_GETDENTS = 141,
  SYS_MUNMAP = 91,
  SYS_MADVISE = 219,
  SYS_FCNT64 = 221,
  SYS_GETDENTS64 = 220,
  SYS_GETTID = 224,
  SYS_CLOCK_NANOSLEEP = 230,
  SYS_FUTEX = 240,
  SYS_SET_THREAD_AREA = 243,
  SYS_EXIT_GROUP = 252,  // diff from arm
  SYS_SET_TID_ADDRESS = 258,
  SYS_STATFS64 = 266,
  SYS_STATX = 383,
  SYS_CLOCK_GETTIME64 = 403,
  SYS_PRINT = 500,
  SYS_PRINT_AT = 501,
  SYS_DEV_READ = 502,
  SYS_DEV_WRITE = 503,
  SYS_DEV_IOCTL = 504,
  SYS_TEST = 505,
  SYS_MAP = 506,
  SYS_UMAP = 507,
  SYS_VALLOC = 508,
  SYS_VFREE = 509,
  SYS_VHEAP = 510,
  SYS_DUMPS = 511,
  SYS_SELF = 512,
  SYS_MEMINFO = 513,
  SYS_THREAD_SELF = 514,
  SYS_THREAD_CREATE = 515,
  SYS_THREAD_DUMP = 516,
  SYS_THREAD_ADDR = 517,
  SYS_THREAD_MAP = 518,
};
#endif

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
	void *(*start_func)(void *);
	void *start_arg;
	volatile int control;
	unsigned long sig_mask[_NSIG/8/sizeof(long)];
}start_args_t;

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
}sched_param_t;


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
#define TP_ADJ(p) ((char *)(p) + sizeof(thread_info_t) + TP_OFFSET)
#else
#define TP_ADJ(p) (p)
#endif

#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define F_GETLK 5
#define F_SETLK 6
#define F_SETLKW 7

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

int sys_clone( int flags, void* stack,int* parent_tid,
              void* tls, int child_tid);

int sys_llseek(int fd, int offset_hi, int offset_lo, off_t* result, int whence);

int sys_umask(int mask);

int sys_stat(const char* path, struct stat* buf);
int sys_fstat(int fd, struct stat* buf);
int sys_self(void* t);

void sys_fn_init();

#endif