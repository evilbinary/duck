/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * 从 sysfn.c 拆出：进程生命周期（exec/fork/clone/exit/wait/pid 族）。
 ********************************************************************/
#include "sysfn.h"

#include "kernel/devfn.h"
#include "kernel/elf.h"
#include "kernel/error.h"
#include "kernel/event.h"
#include "kernel/fd.h"
#include "kernel/kernel.h"
#include "kernel/loader.h"
#include "kernel/thread.h"
#include "kernel/version.h"
#include "kernel/vfs.h"
#include "../loader/loader.h"

/* pipe 实现在 modules/ipc/pipe.c：posix 模块的头文件搜索路径里没有 ipc
 * 目录，跨模块符号按本文件既有做法直接声明（同 sysfn_fs.c 里的 pip_read）。 */
extern int pipe_create_pair(u32 size, vnode_t** read_node, vnode_t** write_node);

#define log_debug

extern vnode_t* root_node;

/* ===================== exec ===================== */

static void exec_params_free(exec_params_t* exec) {
  if (exec == NULL) {
    return;
  }
  if (exec->argv != NULL) {
    for (int i = 0; i < exec->argc; i++) {
      if (exec->argv[i] != NULL) {
        kfree(exec->argv[i]);
      }
    }
    kfree(exec->argv);
  }
  if (exec->envp != NULL) {
    for (int i = 0; i < exec->envc; i++) {
      if (exec->envp[i] != NULL) {
        kfree(exec->envp[i]);
      }
    }
    kfree(exec->envp);
  }
  if (exec->filename != NULL) {
    kfree(exec->filename);
  }
  kfree(exec);
}

static char* exec_dup_string(const char* src) {
  if (src == NULL) {
    return NULL;
  }
  size_t len = kstrlen(src) + 1;
  char* out = kmalloc(len, KERNEL_TYPE);
  if (out == NULL) {
    return NULL;
  }
  kstrcpy(out, src);
  return out;
}

static exec_params_t* exec_params_build(const char* filename, char* const argv[],
                                        char* const envp[]) {
  int argc = 0;
  int envc = 0;
  size_t string_bytes = 0;
  int i = 0;
  while (argv != NULL && argv[i] != NULL) {
    if (argv[i][0] != '\0') {
      argc++;
      string_bytes += kstrlen(argv[i]) + 1;
    }
    i++;
  }
  if (argc == 0) {
    argc = 1;
    string_bytes += kstrlen(filename) + 1;
  }
  if (envp != NULL) {
    for (i = 0; i < 38 && envp[i] != NULL; i++) {
      envc++;
      string_bytes += kstrlen(envp[i]) + 1;
    }
  }

  exec_params_t* exec = kmalloc(sizeof(exec_params_t), KERNEL_TYPE);
  if (exec == NULL) {
    return NULL;
  }
  kmemset(exec, 0, sizeof(exec_params_t));
  exec->filename = exec_dup_string(filename);
  exec->argc = argc;
  exec->envc = envc;
  exec->string_bytes = string_bytes;
  exec->argv = kmalloc(sizeof(char*) * (argc + 1), KERNEL_TYPE);
  exec->envp = kmalloc(sizeof(char*) * (envc + 1), KERNEL_TYPE);
  if (exec->filename == NULL || exec->argv == NULL || exec->envp == NULL) {
    exec_params_free(exec);
    return NULL;
  }
  kmemset(exec->argv, 0, sizeof(char*) * (argc + 1));
  kmemset(exec->envp, 0, sizeof(char*) * (envc + 1));

  int pos = 0;
  for (i = 0; argv != NULL && argv[i] != NULL; i++) {
    if (argv[i][0] == '\0') {
      continue;
    }
    exec->argv[pos] = exec_dup_string(argv[i]);
    if (exec->argv[pos] == NULL) {
      exec_params_free(exec);
      return NULL;
    }
    pos++;
  }
  if (pos == 0) {
    exec->argv[0] = exec_dup_string(filename);
    if (exec->argv[0] == NULL) {
      exec_params_free(exec);
      return NULL;
    }
  }
  for (i = 0; i < envc; i++) {
    exec->envp[i] = exec_dup_string(envp[i]);
    if (exec->envp[i] == NULL) {
      exec_params_free(exec);
      return NULL;
    }
  }
  return exec;
}

u32 sys_exec(char* filename, char* const argv[], char* const envp[]) {
  thread_t* current = thread_current();
  if (current == NULL) {
    log_error("sys exec current is null\n");
    return -1;
  }
  log_debug("sys exec file %s addr %x tid name %s\n", filename, filename,
            current->name);
  // filename = kpage_v2p(filename, 0);
  if (filename == NULL) {
    log_error("sys exec file is null\n");
    return -1;
  }

  if (argv == NULL) {
    log_error("sys exec argv is null %x\n", argv);
    return -1;
  }

  exec_params_t* exec = exec_params_build(filename, argv, envp);
  if (exec == NULL) {
    log_error("sys exec build params failed %s\n", filename);
    return -1;
  }
  log_debug("sys exec %s argc=%d argv0=%s argv1=%s\n", exec->filename,
            exec->argc,
            exec->argv != NULL && exec->argv[0] != NULL ? exec->argv[0] : "-",
            exec->argc > 1 && exec->argv[1] != NULL ? exec->argv[1] : "-");
  {
    /* 【诊断·可删】完整命令行：确认外部程序是怎么被调起来的 */
    static int ex_dbg = 0;
    if (ex_dbg++ < 24) {
      log_debug("EXEC tid=%d %s | [%s][%s][%s][%s][%s]\n", current->id,
                exec->filename,
                exec->argc > 0 && exec->argv[0] ? exec->argv[0] : "-",
                exec->argc > 1 && exec->argv[1] ? exec->argv[1] : "-",
                exec->argc > 2 && exec->argv[2] ? exec->argv[2] : "-",
                exec->argc > 3 && exec->argv[3] ? exec->argv[3] : "-",
                exec->argc > 4 && exec->argv[4] ? exec->argv[4] : "-");
    }
  }

  int fd = (int)sys_open_kernel(exec->filename, 0);
  if (fd < 0) {
    log_error("sys exec file not found %s\n", exec->filename);
    exec_params_free(exec);
    return -1;
  }
  fd_t* f = thread_find_fd_id(current, fd);
  if (f == NULL) {
    log_error("read not found fd %d tid %d\n", fd, current->id);
    sys_close(fd);
    exec_params_free(exec);
    return -1;
  }
  vnode_t* node = f->data;
  if (node == NULL) {
    log_error("sys exec node is null pwd\n");
    sys_close(fd);
    exec_params_free(exec);
    return -1;
  }
  /* 【/proc/self/exe 数据来源】open 时已把解析后的绝对路径存进 fd->name，
   * 在这里固化成 exec->filename：exec("./a.out") 之后 readlink
   * /proc/self/exe 也必须给出绝对路径（Linux 语义）。argv[0] 保持原样。 */
  if (f->name != NULL && f->name[0] == '/') {
    char* abs_path = exec_dup_string((const char*)f->name);
    if (abs_path != NULL) {
      if (exec->filename != NULL) {
        kfree(exec->filename);
      }
      exec->filename = abs_path;
    }
  }
  sys_close(fd);

  if (current->vfs != NULL) {
    if (!vfs_node_is_valid(current->vfs->root)) {
      current->vfs->root = root_node;
    }
    if (!vfs_node_is_valid(current->vfs->pwd)) {
      current->vfs->pwd = current->vfs->root;
    }
  }

  if (current->exec != NULL) {
    exec_params_free(current->exec);
  }
  current->exec = exec;
  current->name = exec->filename;
  /* 【exec 不能覆盖已有 fd】调用方会先把管道 dup2 到 stdin/stdout 再 exec
   * （X server 就是这么把 keymap 喂给 xkbcomp 的）。原实现调 thread_fill_fd
   * 无条件把 0/1/2 换回全局 stdio ⇒ 子进程从串口读输入 ⇒
   * "syntax error: line 1 of stdin"（QEMU 实测，Xorg 因此起不来键盘）。
   * 这里只补空缺的 stdio，已有的 fd 一律保留（POSIX exec 语义）。 */
  thread_ensure_stdio(current);
#if defined(ARM64) || defined(__aarch64__)
  if (run_elf64_thread((long*)exec) < 0) {
    return -1;
  }
#else
  if (run_elf_thread((long*)exec) < 0) {
    return -1;
  }
#endif
  return 0;
}

/* ===================== exit ===================== */

/* 【clear_child_tid 防崩守卫】字段被内核堆污染成垃圾指针（实测
 * 0xffffffffffffff64，nanosleep01 exit_group 崩在 str wzr,[x0,#40]）时，
 * 直接解引用就是内核态缺页。只在当前页表里能解析到物理页才写。 */
static void sys_clear_child_tid(thread_t* current) {
  if (current == NULL || current->clear_child_tid == NULL) {
    return;
  }
  if (current->vm != NULL &&
      page_v2p((u64*)current->vm->upage, current->clear_child_tid) != NULL) {
    *(int*)current->clear_child_tid = 0;
    /* 【必须唤醒】Linux mm_release 语义：写 0 之外还要 futex_wake 该地址。
     * musl 1.2 的线程链表锁由退出线程故意持到 SYS_exit，靠内核这一步
     * 解锁 —— 只写不唤醒 ⇒ 等锁的 joiner/退出线程全部永久阻塞
     * （futex_wake02 join 阶段实测全灭）。 */
    futex_wake_kernel_userspace((u32*)current->clear_child_tid, 0xFFFFFFFFu);
  } else {
    log_error("exit: bad clear_child_tid %lx tid %d name %s\n",
              (unsigned long)(uintptr_t)current->clear_child_tid, current->id,
              current->name != NULL ? current->name : "?");
    current->clear_child_tid = NULL;
  }
}

void sys_exit(int status) {
  thread_t* current = thread_current();
  {
    /* 【诊断·可删】谁以什么码退出 */
    static int xdbg = 0;
    if (xdbg++ < 12) {
      log_debug("EXIT tid=%d code=%d name=%s\n", current != NULL ? current->id : -1,
                status, current != NULL && current->name ? current->name : "?");
    }
  }
#if PROBE_TRACE
  /* 线程退出打点，排查"子进程秒退零输出"时改 PROBE_TRACE=1 */
  {
    static int exit_dbg = 0;
    if (current != NULL && exit_dbg++ < 4000) {
      u64 pc = (current->ctx != NULL && current->ctx->ic != NULL)
                   ? current->ctx->ic->pc
                   : 0;
      log_error("EXIT tid=%d name=%s status=%d pc=%x\n", current->id,
                current->name != NULL ? current->name : "?", status, (u32)pc);
    }
  }
#endif
  if (current != NULL && current->id > 1) {
    log_debug("sys_exit tid=%d status=%d\n", current->id, status);
  }
  sys_clear_child_tid(current);
  thread_exit(current, status);
  // thread_dumps();
  // log_debug("sys exit tid %d %s status %d\n", current->id, current->name,
  //           status);
  // while (current == thread_current()) {
  //   cpu_sti();
  // }
  // cpu_cli();
  sys_thread_self();
  if (current->tinfo != NULL && current->vm != NULL &&
      current->vm->upage != NULL &&
      page_v2p((u64*)current->vm->upage, current->tinfo) != NULL) {
    ((thread_info_t*)current->tinfo)->detach_state = DT_EXITED;
  } else if (current->tinfo != NULL) {
    log_error("exit: bad tinfo %lx tid %d name %s\n",
              (unsigned long)(uintptr_t)current->tinfo, current->id,
              current->name != NULL ? current->name : "?");
  }
  // sys_exit must not return to the caller. Once the current thread is marked
  // stopped, immediately switch away; otherwise execution continues on a dead
  // thread and eventually faults in unrelated code paths.
  schedule_switch();
  for (;;) {
  }
}

void sys_exit_group(int status) {
  {
    /* 【诊断·可删】谁以什么码退出（进程级） */
    static int xgdbg = 0;
    thread_t* cur = thread_current();
    if (xgdbg++ < 12) {
      log_debug("EXITG tid=%d code=%d name=%s\n", cur != NULL ? cur->id : -1,
                status, cur != NULL && cur->name ? cur->name : "?");
    }
  }
#if PROBE_TRACE
  /* 进程退出打点（tid/name/status/pc），排查退出路径时改 PROBE_TRACE=1 */
  {
    static int exitg_dbg = 0;
    thread_t* dbg_t = thread_current();
    if (dbg_t != NULL && exitg_dbg++ < 4000) {
      u64 pc = (dbg_t->ctx != NULL && dbg_t->ctx->ic != NULL) ? dbg_t->ctx->ic->pc : 0;
      log_debug("EXITG tid=%d name=%s status=%d pc=%x\n", dbg_t->id,
                dbg_t->name != NULL ? dbg_t->name : "?", status, (u32)pc);
    }
  }
#endif
  /* 【真正结束整个进程】musl 的 exit()/_Exit() 走 SYS_exit_group；原实现
   * 直接 sys_exit（只退当前线程）✗ —— pthread 后台线程（SDL timer 等）
   * 还活着，ps 里进程一直挂着（gnuboy 按 HOME 打印 "HOME -> exit" 后
   * 不退出就是这个）。这里先做与 sys_exit 相同的当前线程收尾，再由
   * thread_exit_group 停掉同进程其它线程并退出当前线程。 */
  thread_t* current = thread_current();
  sys_clear_child_tid(current);
  if (current != NULL) {
    thread_exit_group(current, status);
    /* 【tinfo 同样要守卫】thread_exit_group 之后 current 可能已被污染/
     * 复用（实测 nanosleep01 exit_group 时 tinfo=0xffffffffffffff3c，
     * 写 detach_state 直接内核态缺页）。detach_state 只是 pthread join
     * 记账，tinfo 不可信时跳过是正确语义。 */
    if (current->tinfo != NULL && current->vm != NULL && current->vm->upage != NULL &&
        page_v2p((u64*)current->vm->upage, current->tinfo) != NULL) {
      ((thread_info_t*)current->tinfo)->detach_state = DT_EXITED;
    } else if (current->tinfo != NULL) {
      log_error("exit_group: bad tinfo %lx tid %d name %s tp %lx ctx %x vm %x\n",
                (unsigned long)(uintptr_t)current->tinfo, current->id,
                current->name != NULL ? current->name : "?",
                (unsigned long)(uintptr_t)current->user_tp,
                (u32)(uintptr_t)current->ctx, (u32)(uintptr_t)current->vm);
    }
  }
  /* 与 sys_exit 相同：不能返回 —— 死线程上继续执行会走飞。 */
  schedule_switch();
  for (;;) {
  }
}

/* ===================== fork / clone ===================== */

int sys_clone(int flags, void* stack, int* parent_tid, void* tls,
              void* child_tid) {
  thread_t* current = thread_current();
  if (current == NULL) {
    log_error("current is null\n");
    return -1;
  }

  // 如果 stack 为 NULL，按 fork 方式处理（vfork 会这样调用）
  if (stack == NULL) {
    thread_t* copy_thread = thread_copy(current, THREAD_VFORK);
    if (copy_thread == NULL) {
      log_error("sys_clone: thread_copy failed\n");
      return -1;
    }
    thread_set_ret(copy_thread, 0);  // 子进程返回 0

#ifdef LOG_DEBUG
    log_debug("-------dump current thread %d %s-------------\n", current->id);
    thread_dump(current, DUMP_DEFAULT | DUMP_CONTEXT);
    log_debug("-------dump clone thread %d-------------\n", copy_thread->id);
    thread_dump(copy_thread, DUMP_DEFAULT | DUMP_CONTEXT);
#endif

    thread_run(copy_thread);
    return copy_thread->id;  // 父进程返回子进程ID
  }

  /*
   * musl pthread: __clone(start, stack, CLONE_VM|...|CLONE_SETTLS, args, ...)
   * ARM clone.s expects child to resume after svc with r0=0 and call start(args).
   * YiYiYa instead starts at start_args on the new stack (same layout).
   * Must share VM (CLONE_VM) or SDL timer/audio threads see a stale heap copy
   * and PREF ABORT (pc=0) walking timers/mutexes.
   */
  u32 tflags = FS_CLONE;
  /* 【vfork(posix_spawn) 必须给独立 VM】CLONE_VM|CLONE_VFORK 的语义是"共享地址
   * 空间、父进程挂起，子进程马上 exec"。Linux 在 exec 时 unshare mm；本内核的
   * run_elf_thread 直接重建当前线程的地址空间，若与父进程 VM_SAME，exec 会把父
   * 进程的页表/映射一起改掉 ⇒ 父进程恢复后在用户态取指炸飞（实测 system()：
   * ls 能跑出结果，但调用者 test-system 随后 PREF ABORT pc=0x1024）。
   * vfork 子进程不需要共享内存（马上 exec），直接给它一份独立 VM（fork 语义）即可。
   * 只有 pthread 那种"非 VFORK 的 CLONE_VM"才真正需要共享地址空间。 */
  if ((flags & CLONE_VM) != 0 && (flags & CLONE_VFORK) == 0) {
    tflags |= VM_SAME;
  } else {
    tflags |= VM_CLONE_ALL;
  }

  /* 【不要再从 stack 顶读 start_func/start_arg】
   * musl 的 pthread_create 会把 struct start_args 放在栈顶，但 posix_spawn
   * 直接传 stack+sizeof stack（栈顶没有 start_args）。若统一按 pthread 布局去读，
   * posix_spawn 会读到未初始化值当入口 ⇒ 子进程跳飞（system() 卡死）。
   * musl 的 arm clone.s 约定：子进程从 svc 之后继续，r0=0，并用 r5/r6 调
   * func(arg)。r5/r6 在父进程 svc 时的帧里已保存，context_inherit_live 会带过来，
   * 所以这里只需 r0=0 并让子进程沿用被复制的 pc 即可，两种调用都成立。 */

  thread_t* copy_thread = thread_copy(current, tflags);
  if (copy_thread == NULL) {
    log_error("sys_clone: thread_copy failed\n");
    return -1;
  }

  if (parent_tid != NULL && (flags & CLONE_PARENT_SETTID) != 0) {
    *parent_tid = copy_thread->id;
  }

  /* CLONE_SETTLS: tls is already TP_ADJ(pthread). Do not keep parent's TP.
   * 【必须以 CLONE_SETTLS 为准】libc clone() 的 ptid/tls/ctid 走 varargs，
   * 调用方少传时是寄存器残渣（实测 tls=4、ptid=8 随 flags=0x100F 满天飞，
   * 无 SETTLS 位）。Linux 语义：无 CLONE_SETTLS 就忽略 tls，子线程沿用
   * thread_copy 继承的父 TP。此前 else-if 无条件采纳 tls，垃圾值 4 直接
   * 变成子线程 TPIDR_EL0 → musl self=负地址 → 第一个 syscall 就崩。 */
  if ((flags & CLONE_SETTLS) != 0 && tls != NULL) {
    copy_thread->user_tp = tls;
#if defined(TLS_ABOVE_TP)
    copy_thread->tinfo = (thread_info_t*)((char*)tls - sizeof(thread_info_t));
#else
    copy_thread->tinfo = tls;
#endif
  }

  /* Run on the pthread stack (mmap), not the parent's process stack. */
  interrupt_context_t* ic = copy_thread->ctx->ksp;
  if (ic != NULL) {
    ic->sp = (u32)stack;
    copy_thread->ctx->usp = (u32)stack;
  }

  thread_set_ret(copy_thread, 0); /* clone child return value (r0=0) */

  /* 【CLONE_CHILD_CLEARTID 必须落地】musl 的 pthread 用 clone 的 ctid 传
   * &__thread_list_lock（线程链表锁）：退出线程持锁走到 SYS_exit，由内核
   * 写 0 + futex 唤醒解锁。原实现 (void)child_tid 直接丢弃 ⇒ pthread 线程
   * clear_child_tid 恒 NULL ⇒ 锁值永远停在持有者 tid 上，join/退出全体
   * 死锁（futex_wake02 join 阶段实测）。 */
  if ((flags & CLONE_CHILD_CLEARTID) != 0 && child_tid != NULL) {
    copy_thread->clear_child_tid = child_tid;
  }
  thread_run(copy_thread);
  return copy_thread->id;
}

/* 【clone3(435)】glibc 2.34+ 的 pthread_create/posix_spawn 优先走 clone3，
 * 缺失时每次创建线程都要先吃一次 ENOSYS 再退回 clone（能用但不干净，
 * 也会刷 "syscall 435 not found handler"）。
 * 参数是 struct clone_args，字段全为 __aligned_u64；这里只取内核关心的
 * 前 8 个，set_tid/cgroup 忽略。与 clone 的语义差异：
 *   1) exit_signal 被拆成独立字段，需并回 flags 低 8 位；
 *   2) stack 是【栈底】，内核要栈顶 ⇒ stack + stack_size；
 *   3) size 小于 64 字节按 EINVAL（Linux 同样校验）。 */
struct clone_args_k {
  u64 flags;
  u64 pidfd;
  u64 child_tid;
  u64 parent_tid;
  u64 exit_signal;
  u64 stack;
  u64 stack_size;
  u64 tls;
  u64 set_tid;
  u64 set_tid_size;
  u64 cgroup;
};

int sys_clone3(void* uargs, size_t size) {
  struct clone_args_k a;
  u64 flags;
  void* stack;
  if (uargs == NULL || size < 64) {
    return -22; /* -EINVAL */
  }
  if (sys_copy_from_user(&a, uargs, 64) < 0) {
    return -14; /* -EFAULT */
  }
  flags = a.flags | (a.exit_signal & 0xffu);
  /* stack == 0 ⇒ 与 fork 同语义（sys_clone 内部按 stack==NULL 处理） */
  stack = (a.stack != 0) ? (void*)(uintptr_t)(a.stack + a.stack_size) : NULL;
  return sys_clone((int)flags, stack, (int*)(uintptr_t)a.parent_tid,
                   (void*)(uintptr_t)a.tls, (void*)(uintptr_t)a.child_tid);
}

int sys_vfork() {
  thread_t* current = thread_current();
  if (current == NULL) {
    log_error("current is null\n");
    return -1;
  }
  thread_t* copy_thread = thread_copy(current, THREAD_VFORK);
  if (copy_thread == NULL) {
    log_error("sys_vfork: thread_copy failed\n");
    return -1;
  }
  thread_set_ret(copy_thread, 0);  // 子进程返回 0

#ifdef LOG_DEBUG
  log_debug("-------dump current thread %d %s-------------\n", current->id);
  thread_dump(current, DUMP_DEFAULT | DUMP_CONTEXT);
  log_debug("-------dump clone thread %d-------------\n", copy_thread->id);
  thread_dump(copy_thread, DUMP_DEFAULT | DUMP_CONTEXT);
#endif

  thread_run(copy_thread);
  return copy_thread->id;  // 父进程返回子进程ID
}

int sys_fork() {
  thread_t* current = thread_current();
  if (current == NULL) {
    log_error("current is null\n");
    return -1;
  }
#ifdef DEBUG
  log_debug("sys fork current kstak size %d\n", current->ctx->ksp_size);
#endif
  // thread_stop(current);
  thread_t* copy_thread = thread_copy(current, THREAD_FORK);
  if (copy_thread == NULL) {
    log_error("sys fork thread copy failed\n");
    return -1;
  }

  thread_set_ret(copy_thread, 0);

  thread_run(copy_thread);
  return copy_thread->id;
}

/* ===================== pid 族 / kill / pipe ===================== */

int sys_pipe(int fds[2]) {
  thread_t* current = thread_current();
  vnode_t* rnode = NULL;
  vnode_t* wnode = NULL;
  fd_t* fd0;
  fd_t* fd1;
  if (fds == NULL || pipe_create_pair(PAGE_SIZE, &rnode, &wnode) < 0) {
    return -1;
  }
  fd0 = fd_open(rnode, DEVICE_TYPE_VIRTUAL, "/dev/pipe/pipe0");
  fd1 = fd_open(wnode, DEVICE_TYPE_VIRTUAL, "dev/pipe/pipe1");
  if (fd0 == NULL || fd1 == NULL) {
    return -1;
  }
  fds[0] = thread_add_fd(current, fd0);
  fds[1] = thread_add_fd(current, fd1);
  return 0;
}

int sys_getpid() {
  thread_t* current = thread_current();
  log_debug("sys get pid %d\n", current->id);
  return current->id;
}

int sys_getppid() {
  thread_t* current = thread_current();
  return current->pid;
}

int sys_set_tid_adress(void* ptr) {
  thread_t* current = thread_current();
  if (current != NULL && current->id > 1) {
    log_debug("sys_set_tid_address tid=%d ptr=%x\n", current->id, ptr);
  }
  if (current != NULL) {
    current->clear_child_tid = ptr;
  }
  return current->id;
}

int sys_gettid() {
  thread_t* current = thread_current();
  return current->id;
}

/* 【单用户内核】getresuid/getresgid：恒 0/0/0（成功）。vfork01 等 LTP 测试
 * 的 setup 调它，ENOSYS 会 TBROK。
 * 写用户指针必须走 sys_copy_to_user（逐页校验是否已映射）：直接
 * *(u32*)ptr 赋值遇到坏指针（实测 0x12）会让内核在 data abort 里崩掉，
 * 整个进程静默消失。 */
static void res_id_store(void* ptr) {
  /* 【必须真的写 0】只在 165/166（aarch64 那其实是 getrusage/getresgid 之外的
   * 错号）注册时，参数根本不是 uid 指针，写内存会打坏调用方的栈 canary，
   * 所以曾经退化成"只返回不写"。现在注册在真实号 148/150 上，三个参数确实
   * 是 uid_t*，不写会留下调用方的栈垃圾：glibc 的 getuid/geteuid 走
   * getresuid 时两个值来自不同栈帧，于是 getuid() != geteuid() 成立，
   * GTK 据此判定"进程以 setuid 运行"并拒绝初始化（lxterminal 退出码 1）。 */
  u32 v = 0;
  if (ptr != NULL && sys_copy_to_user(ptr, &v, sizeof(v)) < 0) {
    /* 坏指针：忽略（Linux 这里会返回 EFAULT，调用方几乎没有能处理的） */
  }
}

int sys_getresuid(void* ruid, void* euid, void* suid) {
  res_id_store(ruid);
  res_id_store(euid);
  res_id_store(suid);
  return 0;
}

int sys_getresgid(void* rgid, void* egid, void* sgid) {
  res_id_store(rgid);
  res_id_store(egid);
  res_id_store(sgid);
  return 0;
}

/* 【getrusage(165)】aarch64 struct rusage = 2 个 timeval(16B) + 14 个 long(8B)
 * = 144 字节，全零返回（本内核没有 CPU 时间统计）。
 * 【千万不要再复用 getresuid 当 165】两个 syscall 的参数完全对不上：
 * getrusage(who, rusage) 的第二参是结构体指针、第三参是寄存器垃圾；按 uid
 * 指针去写 4 字节 0 就是"用垃圾值当地址写内存"。实测后果：Xorg 的
 * getrusage 调用把 TLS 里的 __stack_chk_guard（TP+0x28）写成 0，随后
 * "*** stack smashing detected ***" 直接终止进程。 */
int sys_getrusage(int who, void* usage) {
  u8 zero[144];
  (void)who;
  if (usage == NULL) {
    return 0;
  }
  kmemset(zero, 0, sizeof(zero));
  if (sys_copy_to_user(usage, zero, sizeof(zero)) < 0) {
    return -14; /* -EFAULT */
  }
  return 0;
}

/* 【LTP 新 API 子进程存活的关键】tst_test.c fork_testrun 里子进程
 * SAFE_SETPGID(0,0) 失败即 TBROK 自杀，所有测试收不到结果。 */
int sys_setpgid(int pid, int pgid) {
  log_debug("sys setpgid %d %d\n", pid, pgid);
  return 0;
}

int sys_getpgid(int pid) {
  thread_t* current = thread_current();
  if (pid > 0 && pid != (int)current->id) {
    thread_t* t = thread_find_id(pid);
    if (t == NULL) {
      return -1;
    }
    return (int)t->pid;
  }
  return (int)current->pid;
}

int sys_kill(pid_t pid, int sig) {
  if (pid <= 0) {
    /* 0/负数在 Linux 里指"自己的进程组"：本内核没有进程组语义，
     * 直接当投递成功（此类调用只用于探测，不做实际动作）。 */
    log_debug("kill pgroup %d sig=%d\n", pid, sig);
    return 0;
  }
  thread_t* t = thread_find_id(pid);
  if (t == NULL) {
    /* 【必须返回 -ESRCH，不能返回 -1】用户态普遍用 kill(pid, 0) 探测
     * "这个 pid 还活着吗"。返回 -1 时 musl 只能报 EPERM（dispatch 层把
     * 非 -errno 的返回值当 EPERM），而 X server 读 /tmp/.X0-lock 后的
     * 存活判断是 `kill(pid,0)==0 || errno==EPERM` ⇒ 残留锁文件直接把
     * 新服务器判成"已经在跑"，报 FatalError 后 exit(1)（实测
     * "Server is already active for display 0"，EXITG status=1）。 */
    return -ESRCH;
  }
  /* 【非 SIGKILL 走真正的信号投递】挂到目标线程的 pending 位图，并唤醒它
   * 阻塞中的 syscall（poll/select/epoll/read/write/futex 立刻返回 -EINTR）。
   * 用户态 handler 的执行（信号帧 + sigreturn）尚未实现，因此这里不做
   * "默认动作"（终止/忽略）—— 有 handler 的程序能看到 EINTR，但 handler
   * 本身不会跑；无 handler 的信号只是被消费。
   * 【历史坑】早期版本把任何信号都当立即处决（thread_stop），LTP watchdog 的
   * SIGUSR1 心跳直接把父进程杀了；现在只挂 pending，不会再误杀。
   * SIGKILL(9) 仍按"结束整个进程"处理（下面）。 */
  if (sig != 9 /* SIGKILL */) {
    return thread_signal_send(t, sig);
  }
  thread_t* current = thread_current();
  if (t == current) {
    /* 自杀：只停自己（调用方随后一般就走 exit 了） */
    thread_stop(t);
    return 0;
  }
  /* 【kill 一个进程 = 结束它的【整个进程】并唤醒其父进程】原实现只有
   * thread_stop(t)（停一个线程）✗：
   *   · pthread 后台线程（SDL timer 等）还活着 ⇒ 进程没真正结束；
   *   · thread_stop 不唤醒父 ⇒ 父在 sys_waitpid 的 THREAD_WAITING 里
   *     睡死（实测：kill gnuboy 后 gnuboy 从 ps 消失，但 sh/ymain 一直
   *     wait，整个 system() 链卡住 ✗）。
   * thread_exit_group：停掉 t 同进程（共享 upage）的所有线程；
   * thread_exit(t)：把 t 标退出并【唤醒其父】，父醒来后在圆环里查到
   * 僵尸即返回（sys_waitpid 的实现就是靠重查 + 唤醒 ✓）。 */
  thread_exit_group(t, 0);
  return 0;
}

/* ===================== 会话/凭据 ===================== */

/* 【setsid(157)】创建新会话。本内核无会话概念：直接把调用者当新会话
 * 首进程，返回其 pid（合法 sid 值，调用方只检查不为 -1）。
 * 已是进程组长时应返回 -EPERM——没有进程组语义，不做检查。 */
int sys_setsid(void) {
  thread_t* current = thread_current();
  log_debug("sys setsid tid=%d\n", current->id);
  return (int)current->id;
}

/* 【tgkill(131)】向线程发信号（pthread_kill 底座）。内核无定向线程
 * 信号：tid 忽略，按 tgid 找线程（本内核里"进程 id"就是主线程 id）。 */
int sys_tgkill(int tgid, int tid, int sig) {
  if (tgid <= 0) {
    return -1;
  }
  (void)tid; /* 无线程级信号设施，退化为进程级 */
  return sys_kill(tgid, sig);
}

/* 【tkill(130)】musl 的 raise()/pthread_kill() 实际走的是这个号
 * （src/signal/raise.c、src/thread/pthread_kill.c），不是 tgkill！缺了它
 * LTP 里"自己给自己发信号"的用例整片失效：syscall 130 返回 ENOSYS，
 * 信号根本没发出去（日志 "syscall 130 not found handler"）。 */
int sys_tkill(int tid, int sig) {
  if (tid <= 0) {
    return -1;
  }
  return sys_kill(tid, sig);
}

/* 【setuid/setgid/setgroups】单用户内核：恒成功恒 0（调用方的凭据
 * 切换逻辑依赖返回成功）。 */
int sys_setuid(uid_t uid) {
  (void)uid;
  return 0;
}

int sys_setgid(gid_t gid) {
  (void)gid;
  return 0;
}

int sys_setgroups(int size, const gid_t* list) {
  (void)size;
  (void)list;
  return 0;
}

/* 【getgroups(158)】单用户内核：进程没有附加组。size==0 是"查询需要多大"，
 * 两者都返回 0（0 个组）。缺了它 glibc 启动链（initgroups/NSS）会吃 ENOSYS
 * —— openbox 启动日志里就有 "syscall 158 not found handler"。 */
int sys_getgroups(int size, gid_t* list) {
  (void)size;
  (void)list;
  return 0;
}

/* ===================== wait ===================== */

extern void sys_trace_dump(void);

static pid_t sys_waitpid_trace(pid_t pid, int options, pid_t ret) {
  /* 【诊断·可删】把 Xorg 等子进程的 waitpid 结果与之前的 syscall 序列打出来 */
  static int wp_dbg = 0;
  thread_t* current = thread_current();
  if (wp_dbg++ < 6) {
    log_debug("WAITP tid=%d pid=%d opt=%d ret=%d\n",
              current != NULL ? (int)current->id : -1, (int)pid, options,
              (int)ret);
    sys_trace_dump();
  }
  return ret;
}

pid_t sys_waitpid(pid_t pid, int* wstatus, int options) {
  thread_t* current = thread_current();
  if (current == NULL) {
    return -1;
  }
#ifndef WNOHANG
#define WNOHANG 1
#endif
  for (;;) {
    thread_t* child = thread_find_zombie_child((int)current->id, (int)pid);
    if (child != NULL) {
      /* 【诊断·可删】确认 waitpid 命中的僵尸与其退出码
       * （定位 spawn 链上 system() 返回 -1 的根因） */
      // log_info("waitpid tid=%d pid=%d -> child=%d code=%d state=%d\n",
      //          current->id, (int)pid, (int)child->id, (int)child->code,
      //          child->state);
      pid_t ret = (pid_t)child->id;
      if (wstatus != NULL) {
        /* Linux waitpid 语义：正常退出 status = (code&0xff)<<8 */
        *wstatus = (int)((child->code & 0xff) << 8);
      }
      /* Detach from parent so the same zombie is not returned forever
       * (shell reap_zombies would spin and freeze input). */
      child->pid = (u32)-1;
      return sys_waitpid_trace(pid, options, ret);
    }
    if (!thread_child_exists((int)current->id, (int)pid)) {
      /* 还没有子进程。WNOHANG 必须返回 0：返回 -1 时 musl 会把 errno
       * 设成 EPERM，把前面 accept 的 EAGAIN 盖掉，test-vnc 就退出了。 */
      if (options & WNOHANG) return sys_waitpid_trace(pid, options, 0);
      return sys_waitpid_trace(pid, options, -ECHILD);
    }
    if (options & WNOHANG) {
      return sys_waitpid_trace(pid, options, 0);
    }
    /* 阻塞：置 WAITING 后直接切出去（schedule_switch 交换中断帧，
     * 子进程得以运行）；被 thread_exit 唤醒后继续本循环重试。 */
    current->state = THREAD_WAITING;
    schedule_switch();
  }
}

long sys_wait4(pid_t pid, int* wstatus, int options, struct rusage* rusage) {
  (void)rusage;
  return sys_waitpid(pid, wstatus, options);
}

/* ===================== pidfd_open(434) =====================
 * 进程引用 fd：pidfd_open(pid, flags) 返回一个指向目标进程的 fd，目标退出
 * 后 fd 仍有效。ioctl(PIDFD_GET_INFO) 可查目标的状态（本实现只支持
 * PIDFD_INFO_EXIT 位：目标已退出时 exit_code 带回 wait-status 编码）。
 *
 * 退出信息的两条来路：
 *   1. pidfd_open 时目标已是僵尸（退出但未被 waitpid 收割）：直接从
 *      thread->code 快照；
 *   2. 目标在 pidfd_open 之后才退出：thread_exit 的
 *      thread_pidfd_exit_hook（本文件 sys_pidfd_note_exit）把
 *      (pid, wait-status) 记进登记表 —— 必须经钩子，因为目标被收割后
 *      thread_t 会被回收释放，事后无处可查。
 * fd 载体与 eventfd/epoll 同一套：哨兵 vnode + fd_open(VIRTUAL)，
 * 私有数据挂 vnode->device，close 时摘表 + 释放。 */
struct pidfd_k {
  u32 magic;
  int pid;         /* 目标进程 pid */
  int exited;      /* 目标已退出 */
  int exit_status; /* wait-status 编码 (code&0xff)<<8 */
};
#define PIDFD_K_MAGIC 0x0DFD0000u
/* Linux uapi linux/pidfd.h：PIDFD_INFO_EXIT = 1UL<<3；
 * PIDFD_GET_INFO = _IOWR(0xFF, 11, struct pidfd_info)，sizeof=72
 * ⇒ (3<<30)|(72<<16)|(0xFF<<8)|11 = 0xc048ff0b */
#define PIDFD_INFO_EXIT_ (1UL << 3)
#define PIDFD_GET_INFO_ 0xc048ff0bu

/* 登记表：pidfd_open 之后才退出的目标由钩子按 pid 回填 */
#define PIDFD_TABLE_MAX 16
struct pidfd_entry_k {
  int used;
  int pid;
  int exited;
  int exit_status;
  vnode_t* node;
};
static struct pidfd_entry_k s_pidfds[PIDFD_TABLE_MAX];

void sys_pidfd_note_exit(int pid, int exit_status) {
  int i;
  for (i = 0; i < PIDFD_TABLE_MAX; i++) {
    if (s_pidfds[i].used && s_pidfds[i].pid == pid && !s_pidfds[i].exited) {
      s_pidfds[i].exited = 1;
      s_pidfds[i].exit_status = exit_status;
    }
  }
}

static u32 pidfd_node_close(vnode_t* node) {
  int i;
  if (node == NULL) {
    return 0;
  }
  for (i = 0; i < PIDFD_TABLE_MAX; i++) {
    if (s_pidfds[i].used && s_pidfds[i].node == node) {
      s_pidfds[i].used = 0;
      s_pidfds[i].node = NULL;
    }
  }
  if (node->device != NULL) {
    kfree(node->device);
    node->device = NULL;
  }
  if (node->op != NULL) {
    kfree(node->op);
    node->op = NULL;
  }
  if (node->name != NULL) {
    kfree(node->name);
    node->name = NULL;
  }
  kfree(node);
  return 0;
}

/* pidfd_info 用户态布局（lapi/pidfd.h，aarch64 无 packing）：mask u64 在
 * 最前，exit_code(i32) 在 offset 60，总长 72。 */
struct pidfd_info_k {
  u64 mask;
  u64 cgroupid;
  u32 pid;
  u32 tgid;
  u32 ppid;
  u32 ruid;
  u32 rgid;
  u32 euid;
  u32 egid;
  u32 suid;
  u32 sgid;
  u32 fsuid;
  u32 fsgid;
  i32 exit_code;
  u32 coredump_mask;
  u32 spare;
};

static ssize_t pidfd_ioctl(vnode_t* node, u32 cmd, void* args) {
  struct pidfd_k* p;
  struct pidfd_info_k info;
  if (node == NULL || node->device == NULL) {
    return -1;
  }
  p = (struct pidfd_k*)node->device;
  if (p->magic != PIDFD_K_MAGIC) {
    return -1;
  }
  if (cmd != PIDFD_GET_INFO_) {
    return -1; /* 其余命令未实现 → sys_ioctl 映射 ENOTTY */
  }
  if (args == NULL) {
    return -22; /* -EINVAL */
  }
  kmemset(&info, 0, sizeof(info));
  if (sys_copy_from_user(&info, args, sizeof(info)) < 0) {
    return -14; /* -EFAULT */
  }
  /* 只声明支持 EXIT 位；掩码取与请求的交集（Linux 语义） */
  info.mask &= PIDFD_INFO_EXIT_;
  info.pid = (u32)p->pid;
  info.tgid = (u32)p->pid;
  if (p->exited) {
    info.exit_code = p->exit_status;
  }
  if (sys_copy_to_user(args, &info, sizeof(info)) < 0) {
    return -14;
  }
  return 0;
}

int sys_pidfd_open(unsigned long pid_l, unsigned long flags) {
  thread_t* current = thread_current();
  thread_t* target = NULL;
  vnode_t* node = NULL;
  struct pidfd_k* p = NULL;
  voperator_t* op = NULL;
  fd_t* fd = NULL;
  struct pidfd_entry_k* slot = NULL;
  int pid = (int)pid_l;
  int i;
  int n;
  if (pid <= 0 || flags != 0) {
    return -22; /* -EINVAL */
  }
  if (current == NULL) {
    return -3; /* -ESRCH */
  }
  /* 活线程直接找；找不到再看是不是已退出未收割的僵尸（典型时序：
   * fork 子进程 exit 后、父进程 waitpid 之前开 pidfd） */
  target = thread_find_id(pid);
  if (target == NULL) {
    target = thread_find_zombie_child((int)current->id, pid);
  }
  if (target == NULL) {
    return -3; /* -ESRCH：目标进程不存在 */
  }
  node = vfs_create_node("pidfd", 0);
  p = kmalloc(sizeof(struct pidfd_k), KERNEL_TYPE);
  op = kmalloc(sizeof(voperator_t), KERNEL_TYPE);
  if (node == NULL || p == NULL || op == NULL) {
    kfree(p);
    kfree(op);
    if (node != NULL) {
      kfree(node);
    }
    return -12; /* -ENOMEM */
  }
  kmemset(p, 0, sizeof(struct pidfd_k));
  p->magic = PIDFD_K_MAGIC;
  p->pid = pid;
  p->exited = (target->state == THREAD_STOPPED) ? 1 : 0;
  p->exit_status = p->exited ? (int)((target->code & 0xff) << 8) : 0;
  node->device = p;
  node->op = op;
  op->ioctl = pidfd_ioctl;
  op->close = pidfd_node_close;
  for (i = 0; i < PIDFD_TABLE_MAX; i++) {
    if (!s_pidfds[i].used) {
      slot = &s_pidfds[i];
      break;
    }
  }
  if (slot == NULL) {
    kfree(p);
    kfree(op);
    kfree(node);
    return -12;
  }
  fd = fd_open(node, DEVICE_TYPE_VIRTUAL, "/dev/pidfd");
  if (fd == NULL) {
    kfree(p);
    kfree(op);
    kfree(node);
    return -12;
  }
  n = thread_add_fd(current, fd);
  if (n < 0) {
    fd_close(fd);
    return -12;
  }
  slot->used = 1;
  slot->pid = pid;
  slot->exited = p->exited;
  slot->exit_status = p->exit_status;
  slot->node = node;
  return n;
}
