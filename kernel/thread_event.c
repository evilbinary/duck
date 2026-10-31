/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 *
 * 通用等待协议实现（原 posix/sysfn_pollwait.c 的实现搬到这里）。
 * 协议说明见 thread_wait.h。
 ********************************************************************/
#include "thread_event.h"

#include "arch/cpu.h"
#include "error.h"
#include "kernel.h"
#include "memory.h"
#include "schedule.h"

#define log_debug

struct wait_ent {
  thread_t* t;
  int (*recheck)(void* arg); /* 重算就绪并写回用户态，返回 ready 数 */
  long (*result)(void* arg); /* 可选：真正的 syscall 返回值（read 用） */
  void (*release)(void* arg); /* 释放 arg */
  void* arg;
  long ready;              /* notify 已算出的就绪数，留给 thread_wake 回填用 */
  thread_event_t* ev;      /* 注册在哪个事件上（NULL = 只进全表） */
  int ev_next;             /* 该事件的等待链下一项（wait_tab 下标，-1 结束） */
};

static struct wait_ent* wait_tab = NULL;

/* 就绪时的 syscall 返回值：有 result 回调就用它，否则用 recheck 的返回值 */
static long wait_value(struct wait_ent* e, long ready) {
  if (e->result != NULL) {
    return e->result(e->arg);
  }
  return ready;
}

static void wait_ensure(void) {
  if (wait_tab != NULL) {
    return;
  }
  wait_tab = kmalloc(sizeof(struct wait_ent) * THREAD_EVENT_MAX, KERNEL_TYPE);
  if (wait_tab != NULL) {
    kmemset(wait_tab, 0, sizeof(struct wait_ent) * THREAD_EVENT_MAX);
    /* ev_next = -1 表示"链表结束"：清零后的 0 是合法下标，会被当成自环 */
    for (int i = 0; i < THREAD_EVENT_MAX; i++) {
      wait_tab[i].ev_next = -1;
    }
  }
}

static struct wait_ent* wait_find(thread_t* t) {
  if (t == NULL || wait_tab == NULL) {
    return NULL;
  }
  for (int i = 0; i < THREAD_EVENT_MAX; i++) {
    if (wait_tab[i].t == t) {
      return &wait_tab[i];
    }
  }
  return NULL;
}

static struct wait_ent* wait_slot(thread_t* t) {
  struct wait_ent* e;
  if (t == NULL) {
    return NULL;
  }
  wait_ensure();
  if (wait_tab == NULL) {
    return NULL;
  }
  e = wait_find(t);
  if (e != NULL) {
    return e;
  }
  for (int i = 0; i < THREAD_EVENT_MAX; i++) {
    if (wait_tab[i].t == NULL) {
      return &wait_tab[i];
    }
  }
  return NULL;
}

static void wait_clear(struct wait_ent* e) {
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
  /* 从事件等待链上摘掉：链是按 wait_tab 下标串起来的单链表 */
  if (e->ev != NULL && e->ev->head >= 0) {
    int idx = (int)(e - wait_tab);
    if (e->ev->head == idx) {
      e->ev->head = e->ev_next;
    } else {
      int p = e->ev->head;
      while (p >= 0 && wait_tab[p].ev_next != idx) {
        p = wait_tab[p].ev_next;
      }
      if (p >= 0) {
        wait_tab[p].ev_next = e->ev_next;
      }
    }
  }
  e->ev = NULL;
  e->ev_next = -1;
}

/* recheck 会 copy_to_user，必须切到【等待者】的页表执行。
 * notify 发生在别的线程的上下文里（写方/连接方），当前 TTBR0 不是等待者的。 */
static int wait_recheck_in(thread_t* t, int (*recheck)(void*), void* arg) {
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

int thread_event_wait(int (*recheck)(void* arg), long (*result)(void* arg),
                      void (*release)(void* arg), void* arg, u32 tmo_ms,
                      int has_tmo) {
  return thread_event_wait_on(NULL, recheck, result, release, arg, tmo_ms,
                              has_tmo);
}

int thread_event_wait_on(thread_event_t* ev, int (*recheck)(void* arg),
                         long (*result)(void* arg), void (*release)(void* arg),
                         void* arg, u32 tmo_ms, int has_tmo) {
  thread_t* current = thread_current();
  struct wait_ent* e;
  int ready;

  if (current == NULL || recheck == NULL) {
    if (release != NULL && arg != NULL) {
      release(arg);
    }
    return -1;
  }

  /* 绝大多数调用在这里就返回：本来就有事件，不进等待表 */
  ready = wait_recheck_in(current, recheck, arg);
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

  e = wait_slot(current);
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
  e->ev_next = -1;
  e->ev = ev;
  if (ev != NULL) {
    /* 挂到事件等待链头：notify(ev) 只扫这一条链，不用重算全表 */
    e->ev_next = ev->head;
    ev->head = (int)(e - wait_tab);
  }

  /* 【先置等待态再复查】覆盖"登记之后、睡下之前"事件到达的窗口：
   * 那个时刻 notify 已经能看到本线程在 WAITING，会直接写回保存帧并唤醒；
   * 若 notify 来早了（当时还不是 WAITING），这次复查也能兜住。 */
  current->sleep_counter = 0;
  current->state = THREAD_WAITING;
  current->wait_kind = WAIT_KIND_INTR; /* 允许信号打断（返回 -EINTR） */
  ready = wait_recheck_in(current, recheck, arg);
  if (ready != 0) {
    long v = (ready < 0) ? (long)ready : wait_value(e, ready);
    wait_clear(e);
    current->state = THREAD_RUNNING;
    current->wait_kind = WAIT_KIND_NONE;
    return (int)v;
  }
  if (has_tmo) {
    /* 【+1 抵消 schedule_switch 里的那次递减】schedule_switch() 在切换前会
     * 调 schedule_state()，刚设的 sleep_counter 立刻被减 1 ⇒ 直接设 tmo_ms
     * 实际只睡 tmo_ms-1 个 tick（LTP 定时测试实测每次少 1ms）。+1 之后精确
     * 等于请求的 ms 数。 */
    current->sleep_counter = (int)(tmo_ms > 0 ? tmo_ms : 1) + 1;
  }
  schedule_switch();
  /* 【正常不可达】唤醒方已回填结果并清槽。只有"本 CPU 没有其它可运行线程"
   * 时 schedule_switch 会直接中断返回用户态，这里补一次清理防登记残留。 */
  e = wait_find(current);
  if (e != NULL) {
    wait_clear(e);
  }
  current->wait_kind = WAIT_KIND_NONE;
  return 0;
}

/* 对单个等待者做"重算 + 就绪则唤醒"。返回 1 表示已唤醒。
 * 返回值回填由 thread_wake → thread_event_fill_hook 统一完成（见 wait_fill），
 * 这里只负责"算好就绪数"和"叫醒"——不自己写保存帧。 */
static int wait_try_wake(struct wait_ent* e, thread_t* self) {
  thread_t* t;
  int ready;
  if (e == NULL) {
    return 0;
  }
  t = e->t;
  if (t == NULL || t == self) {
    return 0;
  }
  if (t->state != THREAD_WAITING) {
    return 0;
  }
  /* recheck 会 copy_to_user，必须在等待者的页表下跑 */
  ready = wait_recheck_in(t, e->recheck, e->arg);
  if (ready == 0) {
    return 0; /* 条件还没满足 */
  }
  /* ready < 0：等待已无意义（如 fd 被关），唤醒让 syscall 返回该错误 */
  e->ready = (ready < 0) ? ready : wait_value(e, ready);
  thread_wake(t); /* thread_wake_ex 内部回调 wait_fill 写 x0 并清槽 */
  return 1;
}

/* 按事件唤醒：只扫注册在该事件上的等待者（O(等待者数)，不碰全表）。
 * 这是首选方式 —— 发布方（endpoint 队列、socket 缓冲）自己持有
 * thread_event_t，天然知道该叫醒谁。可在任意线程上下文调用。 */
void thread_event_notify(thread_event_t* ev) {
  thread_t* self = thread_current();
  int i;
  if (wait_tab == NULL) {
    return;
  }
  if (ev == NULL) {
    thread_event_notify_all();
    return;
  }
  for (i = ev->head; i >= 0;) {
    /* 先存 next：唤醒会走 wait_clear，把那一项的 ev_next 清掉 */
    int next = wait_tab[i].ev_next;
    wait_try_wake(&wait_tab[i], self);
    i = next;
  }
}

/* 指定线程唤醒：调用方直接持有目标线程（如"子进程退出唤醒父进程"）。
 * 它不在等待表 / 条件不满足时什么都不做。返回 1 表示已唤醒。 */
int thread_event_notify_thread(thread_t* t) {
  if (wait_tab == NULL || t == NULL) {
    return 0;
  }
  return wait_try_wake(wait_find(t), thread_current());
}

/* 全表广播：唤醒方不知道谁在等（设备 IRQ、pipe 写、pty 输入）时用。
 * 代价是遍历 THREAD_EVENT_MAX 项并对每个等待者重跑 recheck（含切页表），
 * 能定位到具体等待者的场景请改用 thread_event_notify(ev)。 */
void thread_event_notify_all(void) {
  thread_t* self = thread_current();
  if (wait_tab == NULL) {
    return;
  }
  for (int i = 0; i < THREAD_EVENT_MAX; i++) {
    wait_try_wake(&wait_tab[i], self);
  }
}

/* thread_wake 的钩子：把就绪数/超时结果写进等待者的保存帧。
 * 返回 1 表示"这是等待表里的线程，已处理"；0 表示不是（交给 thread_wake_ex
 * 按 futex 语义处理）。
 * 超时路径【不调用 recheck】：tick 上下文切用户页表代价大且没必要 ——
 * 挂起前已把用户态 revents/集合清 0，超时语义只需返回 0。
 * 信号路径回 -EINTR，但【不消费】pending 位 —— 该位要留给"回用户态前"的
 * 真正投递（sysfn_signal.c 建帧跑 handler），在 Linux 上 handler 就是紧跟
 * EINTR 返回执行的。消费掉的话 handler 永远不会跑。 */
static int wait_fill(thread_t* t, int reason) {
  struct wait_ent* e = wait_find(t);
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
  /* 【不在这里打日志】轮询型设备靠"短超时反复醒来"工作（见 sysfn.h 的
   * SYS_POLLED_FD_RECHECK_MS），每次超时都打印会把串口打满并拖慢整机。 */
  if (t->ctx != NULL && t->ctx->ksp != NULL) {
    /* ctx->ksp 是保存帧副本；ctx->ic 指向共享槽，早被别的线程覆盖了 */
    ((interrupt_context_t*)t->ctx->ksp)->x0 = (u64)ret;
  }
  wait_clear(e);
  return 1;
}

void thread_event_init(void) {
  wait_ensure();
  thread_event_fill_hook = wait_fill;
  /* 钩子是"发布方不知道等待者是谁"的兼容入口（pty/pipe/unix 在用），
   * 等价于全表广播。 */
  thread_event_notify_hook = thread_event_notify_all;
}
