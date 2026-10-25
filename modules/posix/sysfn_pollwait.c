/* poll/select/epoll 的阻塞等待协议
 *
 * 本内核的 syscall 不能"睡在内核里"：运行帧放在共享栈区，内核态被换出会让
 * 调用链被别的线程覆盖（见 schedule_sleep 的说明）。futex 走的是另一条路：
 * 登记等待者 → 置 THREAD_WAITING → schedule_switch（内核链被弃），由唤醒方
 * 把返回值写进该线程的保存帧，线程恢复后直接从用户态继续。
 *
 * poll/select/epoll 沿用同一协议，但返回值不是一个常量：必须重新扫描 fd 集合
 * 才知道就绪个数，而且要把 revents / fd_set 写回用户内存。所以等待者登记一个
 * recheck 回调（连同 kmalloc 的参数块），由唤醒方在【等待者的页表】下调用它，
 * 算好就绪数并写回用户结构，最后写保存帧 x0 并唤醒。
 *
 * 唤醒源（unix socket 的 connect/write/close、pipe 的读写）一律调用
 * poll_wait_notify() 做一次全表重算：等待者通常只有一两个，重算成本远低于
 * 维护 fd → 等待者的反向索引。
 */
#include "sysfn.h"

#include "arch/cpu.h"
#include "kernel/error.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "kernel/schedule.h"
#include "kernel/thread.h"

#define log_debug

#define POLLW_MAX 32

struct pollw_ent {
  thread_t* t;
  int (*recheck)(void* arg); /* 重算就绪并写回用户态，返回 ready 数 */
  long (*result)(void* arg); /* 可选：真正的 syscall 返回值（read 用） */
  void (*release)(void* arg); /* 释放 arg */
  void* arg;
  long ready; /* notify 已算出的就绪数，留给 thread_wake 回填用 */
};

/* 就绪时的 syscall 返回值：有 result 回调就用它，否则用 recheck 的返回值 */
static long pollw_value(struct pollw_ent* e, long ready) {
  if (e->result != NULL) {
    return e->result(e->arg);
  }
  return ready;
}

static struct pollw_ent* pollw_tab = NULL;

static void pollw_ensure(void) {
  if (pollw_tab != NULL) {
    return;
  }
  pollw_tab = kmalloc(sizeof(struct pollw_ent) * POLLW_MAX, KERNEL_TYPE);
  if (pollw_tab != NULL) {
    kmemset(pollw_tab, 0, sizeof(struct pollw_ent) * POLLW_MAX);
  }
}

static struct pollw_ent* pollw_find(thread_t* t) {
  int i;
  if (t == NULL || pollw_tab == NULL) {
    return NULL;
  }
  for (i = 0; i < POLLW_MAX; i++) {
    if (pollw_tab[i].t == t) {
      return &pollw_tab[i];
    }
  }
  return NULL;
}

static struct pollw_ent* pollw_slot(thread_t* t) {
  struct pollw_ent* e;
  int i;
  if (t == NULL) {
    return NULL;
  }
  pollw_ensure();
  if (pollw_tab == NULL) {
    return NULL;
  }
  e = pollw_find(t);
  if (e != NULL) {
    return e;
  }
  for (i = 0; i < POLLW_MAX; i++) {
    if (pollw_tab[i].t == NULL) {
      return &pollw_tab[i];
    }
  }
  return NULL;
}

static void pollw_clear(struct pollw_ent* e) {
  if (e == NULL) {
    return;
  }
  if (e->release != NULL && e->arg != NULL) {
    e->release(e->arg);
  }
  e->t = NULL;
  e->recheck = NULL;
  e->result = NULL;
  e->release = NULL;
  e->arg = NULL;
  e->ready = 0;
}

/* recheck 会 copy_to_user，必须切到【等待者】的页表执行。
 * notify 发生在别的线程的上下文里（写方/连接方），当前 TTBR0 不是等待者的。 */
static int pollw_recheck_in(thread_t* t, int (*recheck)(void*), void* arg) {
  int ready;
  u64 saved = 0;
  int switched = 0;
  if (recheck == NULL) {
    return 0;
  }
#if defined(ARM64) && defined(VM_ENABLE)
  if (t != NULL && t->vm != NULL && t->vm->upage != NULL) {
    saved = cpu_read_ttbr0();
    if (saved != (u64)t->vm->upage) {
      cpu_set_page((u64)t->vm->upage);
      switched = 1;
    }
  }
#else
  (void)saved;
  (void)switched;
#endif
  ready = recheck(arg);
  if (switched) {
    cpu_set_page(saved);
  }
  return ready;
}

/* 挂起等待。
 *   recheck(arg) > 0  有事件（0 表示未就绪）
 *   recheck(arg) == 0 无事件
 *   recheck(arg) < 0  放弃等待（返回值即负错误码）
 * result(arg)：可选，就绪时真正的 syscall 返回值（read 的 0 字节 EOF 用）。
 * 返回：本来/刚刚就绪时返回结果；真挂起后【不返回】（由唤醒方写保存帧）。
 * has_tmo=0 表示无限等，只能靠事件唤醒。 */
int poll_wait_suspend(int (*recheck)(void*), long (*result)(void*),
                      void (*release)(void*), void* arg, u32 tmo_ms,
                      int has_tmo) {
  thread_t* current = thread_current();
  struct pollw_ent* e;
  int ready;

  if (current == NULL || recheck == NULL) {
    if (release != NULL && arg != NULL) {
      release(arg);
    }
    return -1;
  }

  /* 绝大多数调用在这里就返回：本来就有事件，不进等待表 */
  ready = pollw_recheck_in(current, recheck, arg);
  if (ready < 0) {
    if (release != NULL && arg != NULL) {
      release(arg);
    }
    return ready;
  }
  if (ready > 0) {
    long v = (result != NULL) ? result(arg) : (long)ready;
    if (release != NULL && arg != NULL) {
      release(arg);
    }
    return (int)v;
  }

  /* 【可中断点】确实没有事件、马上就要睡下时才检查信号：已有事件优先返回。
   * 这里【不消费】pending 位：本函数返回后紧接着就是"回用户态"，投递钩子
   * 会把它变成一次真正的 handler 调用（Linux 语义：handler 先跑，syscall
   * 再以 EINTR 返回给用户）。 */
  if (thread_signal_pending(current)) {
    if (release != NULL && arg != NULL) {
      release(arg);
    }
    return -4; /* -EINTR */
  }

  e = pollw_slot(current);
  if (e == NULL) {
    /* 表满：退化成"报告未就绪"，调用方会在用户态重试（不会死等） */
    if (release != NULL && arg != NULL) {
      release(arg);
    }
    return 0;
  }
  e->t = current;
  e->recheck = recheck;
  e->result = result;
  e->release = release;
  e->arg = arg;
  e->ready = 0;

  /* 【先置等待态再复查】覆盖"登记之后、睡下之前"事件到达的窗口：
   * 那个时刻 notify 已经能看到本线程在 WAITING，会直接写回保存帧并唤醒；
   * 若 notify 来早了（当时还不是 WAITING），这次复查也能兜住。 */
  current->sleep_counter = 0;
  current->state = THREAD_WAITING;
  current->wait_kind = WAIT_KIND_INTR; /* 允许信号打断（返回 -EINTR） */
  ready = pollw_recheck_in(current, recheck, arg);
  if (ready != 0) {
    long v = (ready < 0) ? (long)ready : pollw_value(e, ready);
    pollw_clear(e);
    current->state = THREAD_RUNNING;
    current->wait_kind = WAIT_KIND_NONE;
    return (int)v;
  }
  if (has_tmo) {
    /* 【+1 抵消 schedule_switch 里的那次递减】schedule_switch() 在切换前会
     * 调 schedule_state()，刚设的 sleep_counter 立刻被减 1 ⇒ 直接设 tmo_ms
     * 实际只睡 tmo_ms-1 个 tick（LTP 定时测试实测每次少 1ms：请求 2000us
     * 只睡 1000us、5000us 只睡 4000us）。+1 之后精确等于请求的 ms 数。 */
    current->sleep_counter = (int)(tmo_ms > 0 ? tmo_ms : 1) + 1;
  }
  schedule_switch();
  /* 【正常不可达】唤醒方已回填结果并清槽。只有"本 CPU 没有其它可运行线程"
   * 时 schedule_switch 会直接中断返回用户态，这里补一次清理防登记残留。 */
  e = pollw_find(current);
  if (e != NULL) {
    pollw_clear(e);
  }
  current->wait_kind = WAIT_KIND_NONE;
  return 0;
}

/* 事件发生：重算全表，把就绪者写回并唤醒。可在任意线程上下文调用。
 * 返回值回填由 thread_wake → thread_wait_fill_hook 统一完成（见 pollw_fill），
 * 这里只负责"算好就绪数"和"叫醒"——不自己写保存帧。 */
void poll_wait_notify(void) {
  thread_t* self = thread_current();
  int i;
  if (pollw_tab == NULL) {
    return;
  }
  for (i = 0; i < POLLW_MAX; i++) {
    struct pollw_ent* e = &pollw_tab[i];
    thread_t* t = e->t;
    int ready;
    if (t == NULL || t == self) {
      continue;
    }
    if (t->state != THREAD_WAITING) {
      continue;
    }
    /* recheck 会 copy_to_user，必须在等待者的页表下跑 */
    ready = pollw_recheck_in(t, e->recheck, e->arg);
    if (ready == 0) {
      continue; /* 事件与这个等待者无关 */
    }
    /* ready < 0：等待已无意义（如 fd 被关），唤醒让 syscall 返回该错误 */
    e->ready = (ready < 0) ? ready : pollw_value(e, ready);
    thread_wake(t); /* thread_wake_ex 内部回调 pollw_fill 写 x0 并清槽 */
  }
}

/* thread_wake 的钩子：把就绪数/超时结果写进等待者的保存帧。
 * 返回 1 表示"这是 poll 类等待者，已处理"；0 表示不是（交给 thread_wake_ex
 * 按 futex 语义处理）。
 * 超时路径【不调用 recheck】：tick 上下文切用户页表代价大且没必要 ——
 * 挂起前已把用户态 revents/集合清 0，超时语义只需返回 0。
 * 信号路径回 -EINTR，但【不消费】pending 位 —— 该位要留给"回用户态前"的
 * 真正投递（sysfn_signal.c 建帧跑 handler），在 Linux 上 handler 就是紧跟
 * EINTR 返回执行的。消费掉的话 handler 永远不会跑。 */
static int pollw_fill(thread_t* t, int reason) {
  struct pollw_ent* e = pollw_find(t);
  long ret;
  if (e == NULL) {
    return 0;
  }
  if (reason == WAKE_SIGNAL) {
    ret = -4; /* -EINTR */
  } else if (reason == WAKE_TIMEOUT) {
    ret = 0;
  } else {
    ret = e->ready;
  }
  if (t->ctx != NULL && t->ctx->ksp != NULL) {
    /* ctx->ksp 是保存帧副本；ctx->ic 指向共享槽，早被别的线程覆盖了 */
    ((interrupt_context_t*)t->ctx->ksp)->x0 = (u64)ret;
  }
  pollw_clear(e);
  return 1;
}

void poll_wait_init(void) {
  thread_wait_fill_hook = pollw_fill;
  thread_wait_notify_hook = poll_wait_notify;
}
