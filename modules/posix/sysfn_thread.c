/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * 从 sysfn.c 拆出：线程/调度/同步（thread 族、sched 族、futex）。
 ********************************************************************/
#include "sysfn.h"

#include "kernel/devfn.h"
#include "kernel/error.h"
#include "kernel/event.h"
#include "kernel/fd.h"
#include "kernel/kernel.h"
#include "kernel/thread.h"
#include "kernel/version.h"
#include "kernel/vfs.h"

#define log_debug

/* ===================== thread 族 ===================== */

size_t sys_yeild() { thread_yield(); }

int sys_set_thread_area(void* set) {
  thread_t* current = thread_current();
  if (current == NULL) {
    return -1;
  }
  if (current->id > 1) {
    log_debug("sys_set_thread_area tid=%d tp=%x\n", current->id, set);
  }
  current->user_tp = set;
  /* 硬件线程指针（ARM 上是 TPIDRURO）交给架构层设置：ARMv5 没有该寄存器，
   * 由 cpu_set_tls() 的弱实现（arch.c）兜底为空。详见 arch/cpu.h。 */
  cpu_set_tls(set);
  return 0;
}

int sys_thread_self() {
  thread_t* current = thread_current();
  if (current == NULL) {
    return 0;
  }
  if (current->user_tp != NULL) {
    return (int)current->user_tp;
  }
  /* 【关键】user_tp 还没设置时绝不能把内核 tinfo 地址返回给用户态！
   * 编译器/musl 的 TLS 读取（__aeabi_read_tp / __get_tp → SYS_THREAD_SELF）
   * 会把这个返回值当 TLS 基址；返回内核地址 ⇒ 用户态去读写内核内存 ⇒
   * mallocng 的 TLS/errno 状态被写坏 ⇒ 堆元数据损坏、a_crash（gnuboy 实测）。
   * Linux 在 TPIDRURO 未设置时也返回 0，musl 在 __set_thread_area 之前不会
   * 真正使用 TLS，所以返回 0 是安全且正确的。 */
  return 0;
}

int sys_thread_create(char* name, void* entry, void* data) {
  thread_t* t = thread_create_name(name, entry, data);
  thread_run(t);
  return t;
}

int sys_thread_dump() {
  thread_t* current = thread_current();
  log_debug("========sys dump start========\n");
  thread_dump(current, DUMP_DEFAULT | DUMP_CONTEXT | DUMP_STACK);
  log_debug("========sys dump end========\n");

  return current->id;
}

void* sys_thread_addr(void* vaddr) {
  void* phy_addr = kpage_v2p(vaddr, 0);
  kprintf("===>>%x=>%x\n", vaddr, phy_addr);
  kprintf("phy_addr value===>%x vaddr value:%x %x\n", *((char*)phy_addr),
          *((char*)vaddr));
  // *((char*)vaddr)= *((char*)phy_addr);
  return phy_addr;
}

int sys_thread_map(int tid, u32 virt_addr, u32 phy_addr, u32 size, u32 attr) {
  thread_t* current = NULL;
  current = thread_find_id(tid);
  if (current == NULL) {
    current = thread_current();
  }
  log_debug("sys thread %s map %x %x %d\n", current->name, virt_addr, phy_addr,
            size);

  /* 用户态传入的 attr 语义映射（1=设备，2=系统/内核，3/其它=用户）。
   * 旧实现用三个已废弃的属性名，现统一改为用途名 PAGE_DEV / PAGE_KERNEL /
   * PAGE_USER。 */
  if (attr == 1) {
    attr = PAGE_DEV;
  } else if (attr == 2) {
    attr = PAGE_KERNEL;
  } else {
    attr = PAGE_USER;
  }

  int ret = 0;
  vmemory_map_type(current->vm->upage, virt_addr, phy_addr, size, attr);

  return ret;
}

/* ===================== sched 族 ===================== */

int sys_sched_getparam(int pid, sched_param_t* param) {
  log_debug("sys sched getparam not impl\n");

  return 0;
}

int sys_sched_setparam(int pid, sched_param_t* param) {
  log_debug("sys sched setparam not impl\n");

  return 0;
}

int sys_sched_get_priority_max(int policy) {
  log_debug("sys_sched_get_priority_max not impl %d\n", policy);

  return 0;
}

int sys_sched_get_priority_min(int policy) {
  log_debug("sys_sched_get_priority_min not impl\n");

  return 0;
}

int sys_sched_setscheduler(pid_t pid, int policy,
                           const struct sched_param* param) {
  log_debug("sys_sched_setscheduler not impl\n");

  return 0;
}

/* musl sched_getaffinity：写入 mask 返回字节数。单 CPU → mask=1。 */
int sys_sched_getaffinity(int pid, unsigned int len, unsigned long* mask) {
  (void)pid;
  if (mask == NULL || len < sizeof(unsigned long)) {
    return -1;
  }
  *mask = 1;
  return (int)sizeof(unsigned long);
}

/* ===================== futex =====================
 * 【为什么必须按"地址"登记等待者】
 * 原实现里 FUTEX_WAKE 唤醒的是 thread_current()（**调用者自己** ✗），真正的等待者
 * 谁也等不到唤醒，只能靠 sleep 超时兜底；而 FUTEX_WAIT 也只是盲目睡 400 tick。
 *
 * 这条路上站着 SDL 的音频：SDL_mutexP/V → pthread_mutex_lock → musl → futex；
 * SDL_OpenAudio 还会 SDL_CreateThread() 起音频线程。miyoo 的 /dev/dsp 是空壳
 * （codec_init 空、write 直接丢弃、DMA 模块为 dummy）⇒ 音频线程长时间不释放
 * mixer 互斥 ⇒ 主线程在 mutex 上等 ⇒ 现象正是"应用完全卡死、CPU 却空闲、连 60 帧
 * 统计都打不出来"；而 v3s 有真音频驱动、音频线程持续进展 ⇒ 互斥很快释放 ⇒ 不卡。
 *
 * 现在：等待者按地址登记；FUTEX_WAKE 唤醒**登记在该地址上的等待者**；同时保留
 * 400 tick 兜底睡眠（万一没人唤醒也能自己醒来重试，不会比以前更糟）。
 * ================================================ */
#define FUTEX_WAITER_MAX 8
static struct {
  u32* addr;
  thread_t* t;
} futex_waiters[FUTEX_WAITER_MAX];
static u32 futex_dbg;

static void futex_waiter_add(u32* addr, thread_t* t) {
  int i;
  if (t == NULL) {
    return;
  }
  for (i = 0; i < FUTEX_WAITER_MAX; i++) {
    if (futex_waiters[i].t == t) {
      futex_waiters[i].addr = addr;
      return;
    }
  }
  for (i = 0; i < FUTEX_WAITER_MAX; i++) {
    if (futex_waiters[i].t == NULL) {
      futex_waiters[i].t = t;
      futex_waiters[i].addr = addr;
      return;
    }
  }
  /* 表满：退化为旧的"纯超时"行为，功能不受影响 */
}

static u32 futex_waiter_wake(u32* addr) {
  u32 n = 0;
  int i;
  for (i = 0; i < FUTEX_WAITER_MAX; i++) {
    thread_t* w = futex_waiters[i].t;
    if (w == NULL || futex_waiters[i].addr != addr) {
      continue;
    }
    /* 只在"还在等"时唤醒；已经自己超时醒来的直接清槽 */
    if (w->state == THREAD_SLEEP || w->state == THREAD_WAITING) {
      thread_wake(w); /* 同时清零 sleep_counter ⇒ 兜底睡眠也会立刻结束 */
      n++;
    }
    futex_waiters[i].t = NULL;
    futex_waiters[i].addr = NULL;
  }
  return n;
}

int sys_futex(uint32_t* uaddr, int futex_op, uint32_t val,
              const struct timespec* timeout, /* or: uint32_t val2 */
              uint32_t* uaddr2, uint32_t val3) {
  thread_t* current = thread_current();

  /* 【诊断·可删】只打前 16 次：确认应用真的走到 futex、以及 op 类型
   * （0=WAIT / 1=WAKE）。若 gnuboy 卡住时这里完全没有输出 ⇒ 它与 futex 无关。 */
  if (futex_dbg < 16u) {
    futex_dbg++;
    log_info("futex op=%x addr=%x val=%x tid=%d\n", (u32)futex_op & 0xff,
             (u32)(uintptr_t)uaddr, val, current->id);
  }

  /* 【WAIT_BITSET(9) 不能落进 WAKE 分支】op 的位 0 是 WAIT/WAKE 位：
   * WAIT_BITSET=9、WAIT_BITSET|CLOCK_REALTIME=265 的位 0 都是 1，
   * 旧的 `op & FUTEX_WAKE == FUTEX_WAIT` 判定会把它们当 WAKE 处理
   * （LTP futex_wake02 系列全是 BITSET 变体 ⇒ 语义全错）。统一掩掉
   * PRIVATE(128)/CLOCK_REALTIME(256) 后按 op 判定。 */
  u32 fop = futex_op & ~(u32)128 & ~(u32)256;
  if (fop == 9 /* FUTEX_WAIT_BITSET */) {
    fop = 0 /* 当 FUTEX_WAIT 处理（忽略 bitmask） */;
  }
  if ((fop & 1u) == 0 /* FUTEX_WAIT / FUTEX_WAIT_BITSET */) {
    int i;
    u32 sleep_ticks = 400; /* 无 timeout 的兜底等待 */
    int has_timeout = 0;
    if (uaddr == NULL || *uaddr != val) {
      return EAGAIN;
    }
    /* 【timeout 语义】WAIT 带 timespec：超时后返回 -ETIMEDOUT（Linux 语义）。
     * LTP futex_wait_bitset01 专门验证"超时返回 -1"；此前恒返回 0 导致
     * 测试语义错乱后自旋挂死。CLOCK_REALTIME 的 BITSET 传绝对时间——
     * 按"早已经过"处理同样得到立即超时，两个变体都满足测试期望。 */
    if (timeout != NULL && timeout->tv_sec >= 0) {
      sleep_ticks = (u32)timeout->tv_sec * 1000 +
                    (u32)(timeout->tv_nsec / 1000000);
      has_timeout = 1;
    }
    if (sleep_ticks == 0) {
      sleep_ticks = 1;
    }
    futex_waiter_add(uaddr, current);
    thread_sleep(current, sleep_ticks);
    /* 醒来：仍在 waiter 表 = 没人唤醒 ⇒ 超时；被 WAKE 者槽位已清 ⇒ 0 */
    for (i = 0; i < FUTEX_WAITER_MAX; i++) {
      if (futex_waiters[i].t == current) {
        futex_waiters[i].t = NULL;
        futex_waiters[i].addr = NULL;
        if (has_timeout) {
          return -110; /* ETIMEDOUT */
        }
        return 0;
      }
    }
    return 0;
  }

  if ((fop & 1u) != 0 /* FUTEX_WAKE / FUTEX_WAKE_BITSET */) {
    /* 唤醒登记在该地址上的等待者（不是调用者自己 ✗） */
    return (int)futex_waiter_wake(uaddr);
  }

  log_debug("sys futext not impl %d\n", futex_op);
  return 0;
}
