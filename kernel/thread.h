/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef THREAD_H
#define THREAD_H

#include "arch/arch.h"
#include "config.h"
#include "device.h"
#include "fd.h"
#include "memory.h"
#include "vfs.h"

#define THREAD_CREATE 0
#define THREAD_RUNNING 1
#define THREAD_RUNABLE 2
#define THREAD_STOPPED 3
#define THREAD_WAITING 4
#define THREAD_SLEEP 5
#define THREAD_UNINTERRUPTIBLE 15

#define LEVEL_KERNEL 0
#define LEVEL_KERNEL_SHARE 1
#define LEVEL_USER 3


#define FS_CLONE 1 << 1
#define VM_CLONE_ALL 1 << 2
#define VM_CLONE 1 << 3
#define VM_ALLOC 1 << 4
#define VM_SAME 1 << 5
#define VM_COW 1 << 6  // copy on write

#define THREAD_NEW (VM_CLONE)
#define THREAD_FORK (VM_CLONE_ALL | FS_CLONE)
#define THREAD_CLONE (VM_CLONE_ALL | FS_CLONE)
#define THREAD_VFORK (VM_CLONE_ALL)

#define DUMP_DEFAULT 1
#define DUMP_CONTEXT 2
#define DUMP_STACK 4

#ifndef THREAD_DUMP_STOP_COUNT
#define THREAD_DUMP_STOP_COUNT 2
#endif

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
  int reversed[2];
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

typedef struct thread {
  u32 id;
  u8* name;
  int priority;
  int counter;
  u32 ticks;
  int state;
  int sleep_counter;
  struct thread* next;
  void* data;
  void* exec;
  context_t* ctx;
  vmemory_t* vm;
  u32 pid;
  fd_t** fds;
  u32 fd_size;
  u32 fd_number;
  u32 lock;
  u32 code;
  u32 fault_count;
  u32 faults;
  vfs_t* vfs;
  u32 level;
  u32 cpu_id;
  u32 mem;
  u32 dump_count;
  thread_info_t* info;
} thread_t;

void thread_init();

thread_t* thread_create(void* entry, void* data);
thread_t* thread_create_name(char* name, void* entry, void* data);

thread_t* thread_create_name_level(char* name, void* entry, void* data,
                                   u32 level);

thread_t* thread_create_ex(void* entry, u32 kstack_size, u32 size, void* data,
                           u32 level, u32 flags);

thread_t* thread_create_ex_name(char* name, void* entry, u32 size, void* data,
                                u32 level, u32 flags);
int thread_init_vm(thread_t* copy, thread_t* thread, u32 flags);

void thread_sleep(thread_t* thread, u32 count);

void thread_wake(thread_t* thread);

void thread_add(thread_t* thread);

void thread_remove(thread_t* thread);

void thread_run(thread_t* thread);
void thread_stop(thread_t* thread);
void thread_exit(thread_t* thread, int code);

void thread_yield();
thread_t* thread_current();
thread_t* thread_copy(thread_t* thread, u32 flags);
int thread_add_fd(thread_t* thread, fd_t* fd);
fd_t* thread_find_fd_id(thread_t* thread, u32 fd);
int thread_find_fd_name(thread_t* thread, u8* name);
fd_t* thread_set_fd(thread_t* thread, u32 fd, fd_t* nfd);

thread_t* thread_find_next(thread_t* thread);

thread_t* thread_head();

void thread_run_all();
void thread_reset_stack3(thread_t* thread, u32* stack3);

void thread_fill_fd(thread_t* thread);
void thread_dump_stack(u32* stack, u32 size);
void thread_dump(thread_t* thread, u32 flags);

#endif