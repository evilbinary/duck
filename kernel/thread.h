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

typedef struct exec_params exec_params_t;

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

/* schedule_next: smaller priority runs first; idle must be lowest. */
#define THREAD_PRIORITY_DEFAULT 1
#define THREAD_PRIORITY_IDLE 100


#define FS_CLONE 1 << 1
#define VM_CLONE_ALL 1 << 2
#define VM_CLONE 1 << 3
#define VM_ALLOC 1 << 4
#define VM_SAME 1 << 5
#define VM_COW 1 << 6  // copy on write

#define THREAD_NEW (VM_CLONE)
/* fork/vfork 必须带上已打开的 fd。musl 的 fork 是 clone(SIGCHLD, 0)，
 * stack 为空时走 THREAD_VFORK。不带 FS_CLONE 的话子进程只有 0/1/2，
 * accept(3) 立刻失败，close 也找不到这个 fd。 */
#define THREAD_FORK (VM_CLONE_ALL | FS_CLONE)
/* pthread: share address space + fd table (see CLONE_VM in sys_clone). */
#define THREAD_CLONE (VM_SAME | FS_CLONE)
#define THREAD_VFORK (VM_CLONE_ALL | FS_CLONE)

#define DUMP_DEFAULT 1
#define DUMP_CONTEXT 2
#define DUMP_STACK 4

#ifndef THREAD_DUMP_STOP_COUNT
#define THREAD_DUMP_STOP_COUNT 2
#endif



typedef struct thread {
  u32 id;
  u8* name;
  int priority;
  int priority_base; /* PI 恢复用 */
  int counter;
  u32 ticks;
  int state;
  int sleep_counter;
  struct thread* next;
  struct thread* rt_wait_next; /* rt_mutex 等待链 */
  void* data;
  exec_params_t* exec;
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
  void* tinfo;
  void* user_tp;
  void* clear_child_tid;
  int last_no; /* 诊断：最后进入的 syscall 号（-1 表示还没进过） */
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
/* 【带超时标记的唤醒】等待者（futex / poll / select / epoll）走的是
 * schedule_switch 式阻塞：内核调用链已被弃，恢复后从保存帧直接回用户态
 * ⇒ 返回值只能由唤醒方回填。所有回填逻辑统一收在 thread_wake_ex 里：
 *   timed_out=1：poll 类回 0（无就绪），futex 类回 -ETIMEDOUT
 *   timed_out=0：事件唤醒（futex 的 x0 已由唤醒方写好，poll 类由钩子重算） */
void thread_wake_ex(thread_t* thread, int timed_out);

/* 【等待协议钩子】由 posix 层（sysfn_pollwait.c）在 sys_fn_init 里注册：
 *   thread_wait_fill_hook：thread_wake 内部调用。返回 1 表示"这是 poll 类
 *     等待者，已把就绪数/超时结果写进保存帧并清掉等待登记"；返回 0 表示
 *     普通等待者（futex/waitpid），由 thread_wake_ex 按约定处理。
 *   thread_wait_notify_hook：事件发生（socket 连接/数据、pipe 读写）时调用，
 *     重算所有 poll 类等待者并唤醒就绪者。未注册时为 NULL，调用方要判空。 */
extern int (*thread_wait_fill_hook)(thread_t* t, int timed_out);
extern void (*thread_wait_notify_hook)(void);

void thread_bind_cpu(thread_t* thread, int cpu);
/* 调度切走之后调用：把已经不该留在 from_cpu 上的线程挂到它的 cpu_id。 */
void thread_bind_finish(thread_t* thread, int from_cpu);
void thread_add(thread_t* thread);

void thread_remove(thread_t* thread);

void thread_run(thread_t* thread);
void thread_stop(thread_t* thread);
void thread_recycle_process(void);
void thread_exit(thread_t* thread, int code);
void thread_exit_group(thread_t* thread, int code);

void thread_yield();
thread_t* thread_current();
context_t* thread_current_context();
thread_t* thread_copy(thread_t* thread, u32 flags);
int thread_add_fd(thread_t* thread, fd_t* fd);
fd_t* thread_find_fd_id(thread_t* thread, u32 fd);
int thread_find_fd_name(thread_t* thread, u8* name);
fd_t* thread_set_fd(thread_t* thread, u32 fd, fd_t* nfd);

thread_t* thread_find_next(thread_t* thread);

thread_t* thread_head();

void thread_run_all();
void thread_reset_stack3(thread_t* thread, u32* stack3);
thread_t* thread_find_id(int id);
/* parent_tid: child's pid field; pid>0 exact child, pid<0 any child */
thread_t* thread_find_zombie_child(int parent_tid, int pid);
int thread_child_exists(int parent_tid, int pid);

void thread_fill_fd(thread_t* thread);
u64 thread_user_tp();
void thread_reset_user_context(thread_t* thread, void* entry, void* stack_top);
void thread_dump_stack(u32* stack, u32 size);
void thread_dump(thread_t* thread, u32 flags);

#endif