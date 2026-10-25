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
  /* ---- 信号 ---- */
  u64 sig_pending; /* 位图：bit n = 信号 n 待投递 */
  u64 sig_blocked; /* 位图：被屏蔽的信号 */
  void* sig_acts;  /* struct thread_sigaction[NSIG]，首次 sigaction 时分配 */
  int wait_kind;   /* 当前阻塞点是否"可被信号打断"（见 WAIT_KIND_*） */
} thread_t;

/* 【可中断性】Linux 语义里 poll/select/epoll_wait/read/write 被信号打断
 * 一律失败 EINTR（不受 SA_RESTART 影响），而 waitpid 这类会被 SA_RESTART
 * 重启。本内核暂不实现自动重启，所以只让显式标记的等待点可被打断，
 * waitpid/futex 保持原行为（LTP 的 SAFE_WAITPID 不处理 EINTR）。 */
#define WAIT_KIND_NONE 0
#define WAIT_KIND_INTR 1

/* 【信号号上限】Linux 64 个（NSIG=65，1..64 有效）。
 * 取值留到 65 是为了让 sigaction 表可以直接用信号号当下标（下标 1..64）。 */
#define THREAD_NSIG 65

/* 【位图布局必须和用户态 sigset_t 一致】musl/glibc 的 sigset_t 里信号 n 占
 * 第 n-1 位（SIGINT=2 ⇒ 0x2）。早期实现用第 n 位，导致内核认的"屏蔽 SIGINT"
 * 和用户态传来的掩码对不上——ppoll 的临时 sigmask 完全失效（LTP ppoll01 的
 * MASK_SIGNAL 用例：屏蔽着 SIGINT 却返回 EINTR）。 */
#define THREAD_SIGBIT(sig) (1ull << ((sig) - 1))

/* 内核侧 sigaction（aarch64 ABI：handler/flags/restorer/mask 各 8 字节） */
struct thread_sigaction {
  void* handler;
  u64 flags;
  void* restorer;
  u64 mask;
};

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

/* 【唤醒原因】等待者（futex / poll / select / epoll / read / write）走的是
 * schedule_switch 式阻塞：内核调用链已被弃，恢复后从保存帧直接回用户态
 * ⇒ 返回值只能由唤醒方回填。所有回填逻辑统一收在 thread_wake_ex 里。 */
#define WAKE_EVENT 0   /* 事件唤醒：futex 的 x0 已由唤醒方写好，poll 类钩子重算 */
#define WAKE_TIMEOUT 1 /* 超时：poll 类回 0，futex 类回 -ETIMEDOUT */
#define WAKE_SIGNAL 2  /* 信号打断：回 -EINTR */
void thread_wake_ex(thread_t* thread, int reason);

/* 投递信号（挂到 pending 并唤醒阻塞在该线程上的 syscall，让它返回 -EINTR）。
 * sig=0 只做存活探测。返回 0 成功 / -ESRCH 无线程。 */
int thread_signal_send(thread_t* t, int sig);
/* 有未被屏蔽的待投递信号？ */
int thread_signal_pending(thread_t* t);
/* 取走一个待投递信号号（0 = 没有） */
int thread_signal_take(thread_t* t);
/* 【消费掉"已装 handler"的待投递信号】用户态 handler 的执行（信号帧 +
 * sigreturn）尚未实现，但这些信号在 Linux 上一定是"投递即执行"，不会残留到
 * 下一次 syscall —— 若不清掉，被临时 sigmask 挡住的那一个 SIGINT 会一直挂在
 * pending 上，把后面所有可中断等待点都打成 EINTR。在"解除屏蔽"的时刻
 * （ppoll 返回恢复 sigmask、rt_sigprocmask 放开）调用它，语义上等价于
 * "handler 已经跑完了"。默认动作（SIG_DFL/SIG_IGN）不在这里处理。 */
void thread_signal_flush_handled(thread_t* t);

/* 【等待协议钩子】由 posix 层（sysfn_pollwait.c）在 sys_fn_init 里注册：
 *   thread_wait_fill_hook：thread_wake 内部调用（reason 见 WAKE_*）。返回 1
 *     表示"这是 poll 类等待者，已把结果写进保存帧并清掉等待登记"；返回 0
 *     表示普通等待者（futex/waitpid），由 thread_wake_ex 按约定处理。
 *   thread_wait_notify_hook：事件发生（socket 连接/数据、pipe 读写）时调用，
 *     重算所有 poll 类等待者并唤醒就绪者。未注册时为 NULL，调用方要判空。 */
extern int (*thread_wait_fill_hook)(thread_t* t, int reason);
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