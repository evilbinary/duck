/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * 从 sysfn.c 拆出：杂项/系统信息（print/dumps/meminfo/uname/umask）。
 ********************************************************************/
#include "sysfn.h"

#include "kernel/error.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "kernel/thread.h"
#include "kernel/version.h"

#define log_debug

/* ===================== 输出/调试 ===================== */

int sys_print(char* s) {
  thread_t* current = thread_current();
  // kprintf("sys print %d %s\n", current->id, s);
  kprintf("%s", s);
  return 0;
}

void sys_test() {
  thread_t* current = thread_current();
  kprintf("sys test %d\n", current->id);

  kprintf("-------dump thread %d-------------\n", current->id);
  thread_dump(current, DUMP_DEFAULT | DUMP_CONTEXT);
}

void sys_dumps() {
  thread_t* current = thread_current();
  thread_dumps();
}

int sys_print_at(char* s, u32 x, u32 y) {
  kprintf("%s", s);
  return 0;
}

/* ===================== 系统信息 ===================== */

int sys_mem_info() {
  memory_t* mem = memory_info();
  kprintf("total       %6dk\n", mem->total / 1024);
  kprintf("free        %6dk\n", mem->free / 1024);
  kprintf("kernel used %6dk\n", mem->kernel_used / 1024);
  kprintf("user   used %6dk\n", mem->user_used / 1024);
}

int sys_info(sysinfo_t* info) {
  memory_t* mem = memory_info();
  info->totalram = mem->total;
  info->freeram = mem->free;
  info->procs = thread_count();
  return 0;
}

/* utsname 各字段长度（Linux __NEW_UTS_LEN+1，musl 同布局）。
 * release 走 kernel/version.h 的 KERNEL_VERSION_STRING（单一来源）；
 * machine/nodename 由构建系统注入的 ARM64/RASPI3 等宏推导。 */
#define UTS_FIELD_LEN 65
static void uts_set(char* dst, const char* src) {
  kstrncpy(dst, src, UTS_FIELD_LEN - 1);
  dst[UTS_FIELD_LEN - 1] = '\0';
}

int sys_uname(char* buf) {
  if (buf == NULL) {
    return -1;
  }
  uts_set(buf + 0 * UTS_FIELD_LEN, "Linux"); /* musl/LTP 假定 Linux ABI */
  uts_set(buf + 1 * UTS_FIELD_LEN, UTS_NODENAME);
  uts_set(buf + 2 * UTS_FIELD_LEN, KERNEL_VERSION_STRING);
  uts_set(buf + 3 * UTS_FIELD_LEN, UTS_VERSION);
  uts_set(buf + 4 * UTS_FIELD_LEN, UTS_MACHINE);
  uts_set(buf + 5 * UTS_FIELD_LEN, "");
  return 0;
}

/* ===================== 其它 ===================== */

/* 【身份 syscall】单用户内核恒 root(0)。aarch64 通用号 174-177，每个测试
 * 启动时 tst_test 都会调（此前 "syscall 176 not found" 刷屏）。 */
int sys_getuid() { return 0; }
int sys_geteuid() { return 0; }
int sys_getgid() { return 0; }
int sys_getegid() { return 0; }

int sys_umask(int mask) {
  thread_t* current = thread_current();
  if (current == NULL) {
    log_error("current is null\n");
    return -1;
  }
  // todo

  return mask;
}

int sys_self(void* t) {
  if (t == NULL) return -2;
  thread_t* current = thread_current();
  if (current == NULL) return -1;
  kmemcpy(t, current, sizeof(thread_t));
  return 1;
}

/* ===================== 信号 =====================
 * 内核侧只维护"每个线程一份 sigaction 表 + pending/blocked 位图"：
 * 投递路径由 thread.c 的 thread_signal_send 完成（挂 pending 并唤醒阻塞中的
 * syscall 返回 -EINTR）。用户态 handler 的真正执行（信号帧 + sigreturn）尚未
 * 实现，见 sysfn.c 的说明。 */

#define SIG_BLOCK_ 0
#define SIG_UNBLOCK_ 1
#define SIG_SETMASK_ 2

static struct thread_sigaction* sig_acts_of(thread_t* t) {
  if (t == NULL) {
    return NULL;
  }
  if (t->sig_acts == NULL) {
    t->sig_acts =
        kmalloc(sizeof(struct thread_sigaction) * THREAD_NSIG, KERNEL_TYPE);
    if (t->sig_acts != NULL) {
      kmemset(t->sig_acts, 0, sizeof(struct thread_sigaction) * THREAD_NSIG);
    }
  }
  return (struct thread_sigaction*)t->sig_acts;
}

int sys_rt_sigprocmask(int how, void* set, void* old_set) {
  thread_t* t = thread_current();
  u64 nset = 0;
  if (t == NULL) {
    return -1;
  }
  if (old_set != NULL) {
    if (sys_copy_to_user(old_set, &t->sig_blocked, sizeof(u64)) < 0) {
      return -14; /* -EFAULT */
    }
  }
  if (set != NULL) {
    if (sys_copy_from_user(&nset, set, sizeof(u64)) < 0) {
      return -14;
    }
    if (how == SIG_BLOCK_) {
      t->sig_blocked |= nset;
    } else if (how == SIG_UNBLOCK_) {
      t->sig_blocked &= ~nset;
    } else if (how == SIG_SETMASK_) {
      t->sig_blocked = nset;
    } else {
      return -22; /* -EINVAL */
    }
    /* 注意：【不要】在这里消费 pending。LTP ppoll01 的 SEND_SIGINT_RACE_TEST
     * 依赖"屏蔽期间被挡住的 SIGINT 在 ppoll 临时解除屏蔽后才投递"⇒ 必须在
     * ppoll 里看到 pending 才能返回 EINTR。真正该消费的时机在投递侧（目标没
     * 停在可中断等待里时由 thread_signal_send 直接消费）。 */
  }
  return 0;
}

/* aarch64 内核 ABI 的 struct sigaction：
 *   { void* handler(8); u64 flags(8); void* restorer(8); u64 mask(8) } 共 32B
 * （musl 的 __libc_sigaction 已从用户态布局转成这个格式再进内核）。 */
int sys_rt_sigaction(int signum, const struct sigaction* restrict act,
                     struct sigaction* restrict oldact) {
  thread_t* t = thread_current();
  struct thread_sigaction* acts;
  struct thread_sigaction ka;
  if (t == NULL) {
    return -1;
  }
  if (signum <= 0 || signum >= THREAD_NSIG) {
    return -22; /* -EINVAL */
  }
  acts = sig_acts_of(t);
  if (acts == NULL) {
    return -12; /* -ENOMEM */
  }
  if (oldact != NULL) {
    if (sys_copy_to_user(oldact, &acts[signum], sizeof(ka)) < 0) {
      return -14;
    }
  }
  if (act != NULL) {
    if (sys_copy_from_user(&ka, act, sizeof(ka)) < 0) {
      return -14;
    }
    acts[signum] = ka;
  }
  return 0;
}

/* ===================== 杂项/进程属性 ===================== */

/* 【prctl(167)】进程/线程属性控制。只认真处理 PR_SET_NAME/GET_NAME
 * （线程命名），其余 option no-op 返回 0。 */
#define PR_SET_NAME_ 15
#define PR_GET_NAME_ 16

int sys_prctl(int option, unsigned long a2, unsigned long a3,
              unsigned long a4, unsigned long a5) {
  thread_t* current = thread_current();
  if (current == NULL) {
    return -1;
  }
  (void)a3;
  (void)a4;
  (void)a5;
  if (option == PR_SET_NAME_ && a2 != 0) {
    /* 线程名最长 16 字节（含 '\0'） */
    char name[16];
    u32 len;
    char* nn;
    if (sys_copy_from_user(name, (void*)(unsigned long)a2, sizeof(name)) < 0) {
      return -1;
    }
    name[sizeof(name) - 1] = '\0';
    len = kstrlen(name);
    /* 【绝不能就地写 current->name】那个指针可能是：
     *   - fork/clone 时从父线程继承的同一块内存（thread_init_vm 里
     *     copy->name = thread->name），就地写会把父进程的名字改掉：Xorg 的
     *     InputThread 改名后 ps 里进程名变成 "InputThreadrg/Xorg"；
     *   - exec 的 exec_params->filename（改它等于改 exec 参数）；
     *   - 内核线程的静态字面量（"idle"/"tcpip"，写进只读区直接炸）；
     *   - 父进程名长度决定的缓冲（新名更长还会堆溢出）。
     * 所以照 Linux 语义：改名的线程换一块自己的缓冲（TASK_COMM_LEN 16 字节
     * 上限，内存极小）。fork 不做分配 —— 那是热路径，且改名后指针会被这里
     * 换成新块，父子自然分开。 */
    nn = kmalloc(len + 1, KERNEL_TYPE);
    if (nn == NULL) {
      return -1;
    }
    kmemcpy(nn, name, len + 1);
    current->name = (u8*)nn;
    return 0;
  }
  if (option == PR_GET_NAME_ && a2 != 0) {
    char name[16] = {0};
    if (current->name != NULL) {
      kstrncpy(name, current->name, sizeof(name) - 1);
    }
    if (sys_copy_to_user((void*)(unsigned long)a2, name, sizeof(name)) < 0) {
      return -1;
    }
    return 0;
  }
  return 0;
}

/* 【sigaltstack(132)】信号处理备选栈。无真信号设施：no-op 成功
 * （调用方只检查返回值）。 */
int sys_sigaltstack(const void* ss, void* old_ss) {
  (void)ss;
  (void)old_ss;
  return 0;
}

/* 【getrandom(278)】随机字节填充。无熵源：时钟异或递增计数器的
 * 软件伪随机。 */
int sys_getrandom(void* buf, size_t len, unsigned int flags) {
  static u32 s_rand_state = 0x13572468u;
  u8* p = (u8*)buf;
  u64 ticks;
  extern u64 schedule_get_ticks();
  (void)flags;
  if (buf == NULL || len == 0) {
    return 0;
  }
  ticks = schedule_get_ticks();
  for (size_t i = 0; i < len; i++) {
    s_rand_state ^= (u32)(ticks >> ((i & 7) * 8)) ^ (u32)(i * 2654435761u);
    s_rand_state = s_rand_state * 1664525u + 1013904223u;
    p[i] = (u8)(s_rand_state >> 16);
  }
  return (int)len;
}

/* 【close_range(436)】批量关闭 fd 区间。
 * Linux 语义：区间内不存在的 fd 忽略。 */
int sys_close_range(u32 first, u32 last, unsigned int flags) {
  thread_t* current = thread_current();
  (void)flags;
  if (current == NULL) {
    return -1;
  }
  if (first > last) {
    return -22; /* -EINVAL */
  }
  if (last >= 1024) {
    last = 1023;
  }
  for (u32 fd = first; fd <= last; fd++) {
    if (thread_find_fd_id(current, (int)fd) != NULL) {
      sys_close(fd);
    }
  }
  return 0;
}

/* 【setrlimit(164)】no-op 成功（改动不落地，getrlimit 仍报 NOFILE） */
int sys_setrlimit(int resource, const void* rlim) {
  (void)resource;
  (void)rlim;
  return 0;
}

/* 【set_robust_list(99)/rseq(293)】glibc pthread 初始化调用：no-op 返回 0，
 * 避免每次 not found 噪音。 */
int sys_set_robust_list(void* head, size_t len) {
  (void)head;
  (void)len;
  return 0;
}

int sys_rseq(void* ptr, u32 len, int flags, u32 sig) {
  (void)ptr;
  (void)len;
  (void)flags;
  (void)sig;
  return 0;
}

/* 【name_to_handle_at(264)】文件句柄（fh）是 GPL 之外的"按句柄打开"机制，
 * 本内核的 VFS 没有 inode handle 概念，也无法给出 mount_id。用户态（udev/
 * gio 的设备与挂载点枚举）只把返回值当能力探测：EOPNOTSUPP 会走"不支持"
 * 分支正常降级；返回成功反而会给出垃圾句柄让调用方崩。
 * 注意不要返回 ENOSYS——部分库把 ENOSYS 当"内核太老"直接放弃整条枚举路径。 */
int sys_name_to_handle_at(int dfd, const char* name, void* handle,
                          int* mount_id, int flags) {
  (void)dfd;
  (void)name;
  (void)handle;
  (void)mount_id;
  (void)flags;
  return -95; /* -EOPNOTSUPP */
}
