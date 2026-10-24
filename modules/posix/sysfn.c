/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * 系统调用分发框架与总注册表。
 * 实现按域拆分：sysfn_proc.c（进程）、sysfn_thread.c（线程/调度/futex）、
 * sysfn_mm.c（内存）、sysfn_time.c（时间）、sysfn_misc.c（杂项/信息）、
 * sysfn_fs.c（文件/IO）、sysfn_net.c（网络）。
 ********************************************************************/
#include "sysfn.h"

#include "kernel/devfn.h"
#include "kernel/error.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "kernel/thread.h"

#define log_debug

/* 逐系统调用追踪探针（tid>=6 且只打前 1500 条）由 PROBE_TRACE 统一控制，
 * 默认关闭：它的输出会把用户态程序的真实日志整段淹没。 */

static void* syscall_table[SYSCALL_NUMBER];

extern long xwin_syscall_handler(u32 num, long a1, long a2, long a3, long a4,
                                 long a5, long a6);
#ifdef PERF_MODULE
extern void perf_init_syscall(void** syscall_table);
#endif

int sys_fn_faild_handler(int no, interrupt_context_t* ic) {
  int call_id = context_fn(ic);
  if (call_id == 0xf0005) {
    return sys_fn_call(ic, &sys_set_thread_area);
  }
  if (call_id >= 0x5000 && call_id <= 0x5019) {
    context_ret(ic) =
        xwin_syscall_handler((u32)call_id, (long)context_arg0(ic),
                             (long)context_arg1(ic), (long)context_arg2(ic),
                             (long)context_arg3(ic), (long)context_arg4(ic),
                             (long)context_arg5(ic));
    return 0;
  }
  context_ret(ic) = -ENOSYS;
  log_debug("sys fn faild %x\n", call_id);
  return -1;
}

void sys_fn_call_handler(int no, interrupt_context_t* ic) {
  thread_t* current = thread_current();
  if (current != NULL && current->ctx != NULL) {
    current->ctx->ic = ic;
  }
#if PROBE_TRACE
  {
    static int trace_cnt = 0;
    if (current != NULL && current->id >= 6 && trace_cnt < 1500) {
      log_error("SYSC %d tid=%d no=%d a0=%lx a1=%lx a2=%lx\n", trace_cnt++,
                current->id, (int)context_fn(ic),
                (u64)context_arg0(ic), (u64)context_arg1(ic),
                (u64)context_arg2(ic));
    }
  }
  /* glibc 栈保护 canary 位于 TLS 的 [TP+0x28]。每次进内核读一次，与上次比对：
   * 值变了就说明"上一个系统调用"把用户栈/TLS 写坏了，直接点名。 */
  {
    static u64 guard_ref = 0;
    static u64 guard_tp = 0;
    static int guard_init = 0;
    static int last_no = -1;
    u64 tp = cpu_get_tls();
    if (tp != 0) {
      u64 g = 0;
      if (sys_copy_from_user(&g, (const void*)(uintptr_t)(tp + 0x28), sizeof(g)) >=
          0) {
        if (!guard_init || tp != guard_tp) {
          guard_init = 1;
          guard_tp = tp;
          guard_ref = g;
        } else if (g != guard_ref) {
          log_error("GUARD tp=%lx %lx -> %lx after syscall no=%d tid=%d\n", tp,
                    guard_ref, g, last_no, current != NULL ? current->id : -1);
          guard_ref = g;
        }
      }
    }
    last_no = (int)context_fn(ic);
  }
#endif
  void* fn = syscall_table[context_fn(ic)];
  if (fn != NULL) {
    if (context_fn(ic) == SYS_EXEC) {
      u32 ret = sys_exec((char*)context_arg0(ic),
                         (char* const*)context_arg1(ic),
                         (char* const*)context_arg2(ic));
      if ((int)ret < 0) {
        context_ret(ic) = ret;
      } else {
        thread_t* current = thread_current();
        if (current != NULL && current->ctx != NULL && current->ctx->ksp != NULL) {
          kmemmove(ic, current->ctx->ksp, sizeof(interrupt_context_t));
          log_debug("sys exec return pc=%x sp=%x tid=%d\n", ic->pc,
                    current->ctx->usp, current->id);
        }
      }
    } else {
      sys_fn_call((ic), fn);
    }
  } else {
    context_ret(ic) = -ENOSYS;
    log_warn("syscall %d not found handler\n", context_fn(ic));
  }
}

void* sys_fn_get(int no) {
  if (no < 0 || no >= SYSCALL_NUMBER) {
    return NULL;
  }
  return syscall_table[no];
}

void sys_fn_init() {
  sys_fn_regist_faild(sys_fn_faild_handler);
  sys_fn_regist_handler(sys_fn_call_handler);

  syscall_table[SYS_READ] = &sys_read;
  syscall_table[SYS_WRITE] = &sys_write;
  syscall_table[SYS_YIELD] = &sys_yeild;
  syscall_table[SYS_PRINT] = &sys_print;
  syscall_table[SYS_PRINT_AT] = &sys_print_at;
  syscall_table[SYS_IOCTL] = &sys_ioctl;
#if defined(ARM64) || defined(__aarch64__) || defined(ARM)
  syscall_table[SYS_OPEN] = &sys_open_dispatch;
  syscall_table[SYS_OPENAT] = &sys_open_dispatch;
#else
  syscall_table[SYS_OPEN] = &sys_open;
#endif
  syscall_table[SYS_CLOSE] = &sys_close;
  syscall_table[SYS_DEV_READ] = &dev_read;
  syscall_table[SYS_DEV_WRITE] = &dev_write;
  syscall_table[SYS_DEV_IOCTL] = &dev_ioctl;
  syscall_table[SYS_EXEC] = &sys_exec;
  syscall_table[SYS_TEST] = &sys_test;
  syscall_table[SYS_EXIT] = &sys_exit;
  syscall_table[SYS_STOP] = &sys_exit;
  syscall_table[SYS_MAP] = &sys_vmap;
  syscall_table[SYS_UMAP] = &sys_vumap;
  syscall_table[SYS_SEEK] = &sys_seek;
  syscall_table[SYS_VALLOC] = &sys_valloc;
  syscall_table[SYS_VFREE] = &sys_vfree;
  syscall_table[SYS_VHEAP] = &sys_vheap;
  syscall_table[SYS_FORK] = &sys_fork;
  syscall_table[SYS_PIPE] = &sys_pipe;
#ifdef ARM
  /* pipe2 (ARM nr=359)：musl 的 system()/spawn 路径会调用。直接复用 pipe，
   * 忽略 flags（内核 pipe 无 O_CLOEXEC/O_NONBLOCK 语义）。 */
  syscall_table[359] = &sys_pipe;
#endif
  syscall_table[SYS_GETPID] = &sys_getpid;
  syscall_table[SYS_GETPPID] = &sys_getppid;
  syscall_table[SYS_DUP] = &sys_dup;
  syscall_table[SYS_DUP2] = &sys_dup2;
  /* 【aarch64】dup3=24：aarch64 无 dup2，musl dup2() 走 dup3(fd,newfd,flags)。
   * 忽略 flags（O_CLOEXEC 语义没有），行为等同 dup2。 */
  syscall_table[24] = &sys_dup2;
  syscall_table[SYS_READDIR] = &sys_readdir;
  syscall_table[SYS_NEWSELECT] = &sys_select;
  syscall_table[SYS_BRK] = &sys_brk;

  syscall_table[SYS_READV] = &sys_readv;
  syscall_table[SYS_WRITEV] = &sys_writev;
  syscall_table[SYS_CHDIR] = &sys_chdir;
  syscall_table[SYS_MMAP2] = &sys_mmap2;
  syscall_table[SYS_MPROTECT] = &sys_mprotect;
  syscall_table[SYS_RT_SIGPROCMASK] = &sys_rt_sigprocmask;
  syscall_table[SYS_RT_SIGACTION] = &sys_rt_sigaction;

  syscall_table[SYS_ALARM] = &sys_alarm;
  // aarch64 uses *at syscalls for many path operations
#if defined(ARM64) || defined(__aarch64__)
  syscall_table[SYS_UNLINK] = &sys_unlinkat;
  syscall_table[SYS_RENAME] = &sys_renameat;
#else
  syscall_table[SYS_UNLINK] = &sys_unlink;
  syscall_table[SYS_RENAME] = &sys_rename;
#endif

#if defined(ARM64) || defined(__aarch64__)
  /* 37 在 aarch64 真实 ABI 里是 linkat（arm64 没有 alarm，musl 的 alarm() 走
   * setitimer）。要写在 SYS_ALARM 之后才不会被同号的 alarm 覆盖。
   * 276 = renameat2，flags 只支持 0，等价 renameat。 */
  syscall_table[SYS_LINKAT] = &sys_linkat;
  syscall_table[SYS_RENAMEAT2] = &sys_renameat2;
  syscall_table[SYS_UTIMENSAT] = &sys_utimensat;
#endif

  syscall_table[SYS_SET_THREAD_AREA] = &sys_set_thread_area;
  syscall_table[SYS_DUMPS] = &sys_dumps;

  syscall_table[SYS_GETDENTS64] = &sys_getdents64;
  syscall_table[SYS_GETDENTS] = &sys_getdents64;
  syscall_table[SYS_MUNMAP] = &sys_munmap;

  syscall_table[SYS_FCNT64] = &sys_fcntl64;
  /* 【aarch64】163=getrlimit / 261=prlimit64：musl sysconf(_SC_OPEN_MAX)
   * 依赖它们，没注册时栈垃圾当 open_max ⇒ dup201 18.8 亿次 fcntl 卡死。
   * （实测 warn 顺序：syscall 261 not found → 163 not found → fcntl64 风暴） */
  syscall_table[163] = &sys_getrlimit64;
  syscall_table[261] = &sys_prlimit64;
  syscall_table[SYS_GETCWD] = &sys_getcwd;
  syscall_table[SYS_CHDIR] = &sys_chdir;
  syscall_table[SYS_FCHDIR] = &sys_fchdir;
  syscall_table[SYS_FCHOWNAT] = &sys_fchownat; /* musl chown/lchown/fchown */
  syscall_table[SYS_FCHMODAT] = &sys_fchmodat; /* musl chmod */
  syscall_table[SYS_FCHMOD] = &sys_fchmod;     /* fchmod（FAT 无权限位，no-op 成功） */
  syscall_table[SYS_FTRUNCATE] = &sys_ftruncate; /* musl ftruncate/truncate */
  syscall_table[SYS_CLONE] = &sys_clone;
#if defined(ARM64)
  // ARM64 uses lseek (62) which returns 64-bit offset directly.
  // _llseek is an ARM32 legacy syscall, map it to lseek for compatibility.
  syscall_table[SYS_LLSEEK] = &sys_seek;
#else
  syscall_table[SYS_LLSEEK] = &sys_llseek;
#endif

  syscall_table[SYS_UMASK] = &sys_umask;

  // aarch64 syscall 79 is newfstatat
#if defined(ARM64) || defined(__aarch64__)
  syscall_table[SYS_STAT] = &sys_stat_dispatch;
#else
  syscall_table[SYS_STAT] = &sys_stat;
#endif
  syscall_table[SYS_FSTAT] = &sys_fstat;
  syscall_table[SYS_SELF] = &sys_self;

  syscall_table[SYS_CLOCK_NANOSLEEP] = &sys_clock_nanosleep;
  syscall_table[SYS_NANOSLEEP] = &sys_nanosleep;
  /* 【musl 真正用的号】time64 构建的 musl 里，nanosleep()/usleep() 发的是
   * clock_nanosleep_time64(407)，失败回退 clock_nanosleep_time32(265)。
   * 处理函数可直接复用：本内核 struct timespec 的 tv_sec 为 int64、
   * tv_nsec 为 long，偏移(0/8)与 time64 布局一致。 */
#ifdef SYS_CLOCK_NANOSLEEP_TIME32
  syscall_table[SYS_CLOCK_NANOSLEEP_TIME32] = &sys_clock_nanosleep;
#endif
#ifdef SYS_CLOCK_NANOSLEEP_TIME64
  syscall_table[SYS_CLOCK_NANOSLEEP_TIME64] = &sys_clock_nanosleep;
#endif

  syscall_table[SYS_MREMAP] = &sys_mremap;
  syscall_table[SYS_STATX] = &sys_statx;
  syscall_table[SYS_CLOCK_GETTIME64] = &sys_clock_gettime64;
  /* aarch64 time64 变体（musl 某些路径会用 403） */
  syscall_table[SYS_CLOCK_GETTIME64 + 290] = &sys_clock_gettime64;

  syscall_table[SYS_SYSINFO] = &sys_info;
  syscall_table[SYS_MEMINFO] = &sys_mem_info;
  syscall_table[SYS_THREAD_SELF] = &sys_thread_self;

  syscall_table[SYS_SET_TID_ADDRESS] = &sys_set_tid_adress;

  syscall_table[SYS_EXIT_GROUP] = &sys_exit_group;
  // aarch64 syscall 78 is readlinkat
#if defined(ARM64) || defined(__aarch64__)
  syscall_table[SYS_READLINK] = &sys_readlink_dispatch;
#else
  syscall_table[SYS_READLINK] = &sys_readlink;
#endif

  syscall_table[SYS_MADVISE] = &sys_madvice;

  syscall_table[SYS_THREAD_CREATE] = &sys_thread_create;
  syscall_table[SYS_THREAD_DUMP] = &sys_thread_dump;

  syscall_table[SYS_KILL] = &sys_kill;
  syscall_table[SYS_THREAD_ADDR] = &sys_thread_addr;

  syscall_table[SYS_FUTEX] = &sys_futex;
  // aarch64 syscall 34 is mkdirat
#if defined(ARM64) || defined(__aarch64__)
  syscall_table[SYS_MKDIR] = &sys_mkdirat;
#else
  syscall_table[SYS_MKDIR] = &sys_mkdir;
#endif
  // aarch64 syscall 48 is faccessat
#if defined(ARM64) || defined(__aarch64__)
  syscall_table[SYS_ACESS] = &sys_access_dispatch;
#else
  syscall_table[SYS_ACESS] = &sys_access;
#endif

  syscall_table[SYS_GETTID] = &sys_gettid;
  /* 【aarch64】165=getresuid / 166=getresgid：vfork01 等测试的 setup 调
   * getresuid，未注册返回 ENOSYS ⇒ TBROK。单用户内核恒返回 0/0/0。 */
  syscall_table[165] = &sys_getresuid;
  syscall_table[166] = &sys_getresgid;
  syscall_table[SYS_THREAD_MAP] = &sys_thread_map;
  syscall_table[SYS_FSTAT64] = &sys_fstat64;
  syscall_table[SYS_STATFS64] = &sys_statfs64;
  syscall_table[SYS_SCHED_GETPARAM] = &sys_sched_getparam;
  syscall_table[SYS_SCHED_SETPARAM] = &sys_sched_setparam;
  syscall_table[SYS_SCHED_SETSCHEDULER] = &sys_sched_setscheduler;
  syscall_table[SYS_SCHED_GET_PRIORITY_MAX] = &sys_sched_get_priority_max;
  syscall_table[SYS_SCHED_GET_PRIORITY_MIN] = &sys_sched_get_priority_min;
  syscall_table[SYS_SCHED_GETAFFINITY] = &sys_sched_getaffinity;

  syscall_table[SYS_SETITIMER] = &sys_setitimer;
  syscall_table[SYS_MSYNC] = &sys_msync;
  syscall_table[SYS_SETPGID] = &sys_setpgid;
  syscall_table[SYS_GETPGID] = &sys_getpgid;
  syscall_table[SYS_UNAME] = &sys_uname;
  syscall_table[SYS_MLOCKALL] = &sys_mlockall;
  /* 【aarch64 通用号】getuid/geteuid/getgid/getegid(174-177)：tst_test 每个
   * 测试启动都调，此前 "syscall 176 not found" 刷屏。单用户恒 0。 */
#if defined(ARM64) || defined(__aarch64__)
  syscall_table[174] = &sys_getuid;
  syscall_table[175] = &sys_geteuid;
  syscall_table[176] = &sys_getgid;
  syscall_table[177] = &sys_getegid;
#endif

  syscall_table[SYS_WAIT4] = &sys_wait4;

  /* 【标准 POSIX 接口组】轮询/事件 fd/会话/杂项 */
  syscall_table[SYS_PPOLL] = &sys_ppoll;
  syscall_table[SYS_SETSID] = &sys_setsid;
  syscall_table[SYS_PRCTL] = &sys_prctl;
  syscall_table[SYS_SIGALTSTACK] = &sys_sigaltstack;
  syscall_table[SYS_TGKILL] = &sys_tgkill;
  syscall_table[SYS_GETRANDOM] = &sys_getrandom;
  syscall_table[SYS_SETRLIMIT] = &sys_setrlimit;
  syscall_table[SYS_SETUID] = &sys_setuid;
  syscall_table[SYS_SETGID] = &sys_setgid;
  /* setresuid/setresgid/setreuid：本内核没有权限模型，按"永远成功"处理。
   * 参数多余的部分被忽略（sys_setuid 只读第一个）。 */
  syscall_table[SYS_SETRESUID] = &sys_setuid;
  syscall_table[SYS_SETRESGID] = &sys_setgid;
  syscall_table[SYS_SETREUID] = &sys_setuid;
  syscall_table[SYS_SETGROUPS] = &sys_setgroups;
  syscall_table[SYS_VHANGUP] = &sys_setsid;
  syscall_table[SYS_EVENTFD2] = &sys_eventfd2;
  syscall_table[SYS_EPOLL_CREATE1] = &sys_epoll_create1;
  syscall_table[SYS_EPOLL_CTL] = &sys_epoll_ctl;
  syscall_table[SYS_EPOLL_PWAIT] = &sys_epoll_pwait;
  syscall_table[SYS_INOTIFY_INIT1] = &sys_inotify_init1;
  syscall_table[SYS_INOTIFY_ADD_WATCH] = &sys_inotify_add_watch;
  syscall_table[SYS_INOTIFY_RM_WATCH] = &sys_inotify_rm_watch;
  syscall_table[SYS_CLOSE_RANGE] = &sys_close_range;
  syscall_table[SYS_SET_ROBUST_LIST] = &sys_set_robust_list;
  syscall_table[SYS_RSEQ] = &sys_rseq;
  syscall_table[SYS_SENDMSG] = &sys_sendmsg;
  syscall_table[SYS_RECVMSG] = &sys_recvmsg;

  // Initialize network syscalls
  sys_fn_net_init((void**)syscall_table);
#ifdef PERF_MODULE
  perf_init_syscall((void**)syscall_table);
#endif
}

