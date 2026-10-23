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
/* 【动态分配等待者表】futex_wake02 一次挂 55 个线程在同一 futex 上；
 * 静态 8 槽时多余等待者登记失败退化成 30s 盲睡 ⇒ 唤醒计数全错 + join
 * 永久重试挂死。表只在系统调用路径（线程上下文，可睡眠）访问，首次
 * 使用时 kmalloc，分配失败或未分配时登记退化成"纯超时"行为（不阻塞
 * 功能，只是唤醒语义降级）。 */
#define FUTEX_WAITER_MAX 512
static struct {
  u32* addr;
  thread_t* t;
} * futex_waiters = NULL;
static u32 futex_dbg;

static void futex_waiter_ensure(void) {
  if (futex_waiters != NULL) {
    return;
  }
  futex_waiters = kmalloc(sizeof(*futex_waiters) * FUTEX_WAITER_MAX, KERNEL_TYPE);
  if (futex_waiters != NULL) {
    kmemset(futex_waiters, 0, sizeof(*futex_waiters) * FUTEX_WAITER_MAX);
  }
}

static void futex_waiter_add(u32* addr, thread_t* t) {
  int i;
  if (t == NULL) {
    return;
  }
  futex_waiter_ensure();
  if (futex_waiters == NULL) {
    return; /* 分配失败：退化为"纯超时"行为 */
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

static u32 futex_waiter_wake(u32* addr, u32 max_wake) {
  u32 n = 0;
  int i;
  /* 【按 val 限量唤醒】Linux 语义 FUTEX_WAKE 的 val = 最多唤醒几个；
   * futex_wake02 逐个验证 wake(1)..wake(10) 的返回值 ⇒ 忽略 val 一次
   * 全唤醒会让计数与 flags 全错。 */
  if (futex_waiters == NULL) {
    return 0;
  }
  for (i = 0; i < FUTEX_WAITER_MAX && n < max_wake; i++) {
    thread_t* w = futex_waiters[i].t;
    if (w == NULL || futex_waiters[i].addr != addr) {
      continue;
    }
    /* 只在"还在等"时唤醒；已经自己超时醒来的直接清槽 */
    if (w->state == THREAD_SLEEP || w->state == THREAD_WAITING) {
      /* 【回填返回值】WAITING 是 schedule_switch 式阻塞：内核调用链被弃，
       * 唤醒后线程从保存帧直接回用户态 —— 必须由唤醒方把返回值写进
       * 保存帧（与 thread_exit 代收割 waitpid 的同款机制）。 */
      if (w->state == THREAD_WAITING && w->ctx != NULL &&
          w->ctx->ksp != NULL) {
        ((interrupt_context_t*)w->ctx->ksp)->x0 = 0;
      }
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
  {
    /* 【定向探针·可删】全量打会淹没在框架噪音里：只打 WAKE 类（join 退出
     * 唤醒）和 tid<=10 的主线程操作。总量有上限，长期运行不刷屏。 */
    futex_dbg++;
    if (futex_dbg < 400u &&
        (((futex_op & 1u) != 0 && futex_dbg > 300u) || current->id <= 10u)) {
      u64 pc = (current->ctx != NULL && current->ctx->ic != NULL)
                   ? current->ctx->ic->pc
                   : 0;
      u64 lr = (current->ctx != NULL && current->ctx->ic != NULL)
                   ? current->ctx->ic->lr
                   : 0;
      log_info("futex op=%x addr=%x val=%x tid=%d pc=%x lr=%x\n",
               (u32)futex_op & 0xff, (u32)(uintptr_t)uaddr, val, current->id,
               (u32)pc, (u32)lr);
    }
  }

  /* 【WAIT_BITSET(9) 不能落进 WAKE 分支】op 的位 0 是 WAIT/WAKE 位：
   * WAIT_BITSET=9、WAIT_BITSET|CLOCK_REALTIME=265 的位 0 都是 1，
   * 旧的 `op & FUTEX_WAKE == FUTEX_WAIT` 判定会把它们当 WAKE 处理
   * （LTP futex_wake02 系列全是 BITSET 变体 ⇒ 语义全错）。统一掩掉
   * PRIVATE(128)/CLOCK_REALTIME(256) 后按 op 判定。 */
  u32 fop = futex_op & ~(u32)128 & ~(u32)256;
  int is_bitset = (fop == 9) /* WAIT_BITSET：绝对超时 + 忽略 bitmask */;
  if (fop == 9 /* FUTEX_WAIT_BITSET */) {
    fop = 0 /* 当 FUTEX_WAIT 处理（忽略 bitmask） */;
  }
  if ((fop & 1u) == 0 /* FUTEX_WAIT / FUTEX_WAIT_BITSET */) {
    /* 【无 timeout = 真·永久阻塞】Linux 语义。旧实现 thread_sleep 只是
     * "记账"（syscall 立即回用户态，tick 边界才真正换出）⇒ musl 的等待
     * 循环在同一个 tick 内把返回值当"已唤醒"继续跑：futex_wake02 的测试
     * 线程 futex_wait 一返回就退出，wait_for_threads 永远凑不齐 ⇒ 挂死。
     * 现照 sys_waitpid 模式：登记 → 置 WAITING → schedule_switch() 换出，
     * 内核链被弃；被 WAKE 回填 x0=0 或 tick 到期回填 x0=-ETIMEDOUT 后
     * 从保存帧直接回用户态（schedule.c schedule_state 的 WAITING 分支）。 */
    int has_timeout = 0;
    if (uaddr == NULL || *uaddr != val) {
      return EAGAIN;
    }
    u32 sleep_ticks = 0;
    if (timeout != NULL) {
      /* 【timespec 两种布局】SYS_futex(98) 的调用方有两种：
       *  a) futextest.h/LTP 直发：old_timespec32 = 8 字节打包 {i32 sec, i32 nsec}；
       *  b) musl __timedwait 内部：16 字节 {i64 sec, i64 nsec}（相对时长）。
       * 区分：16B 的小正数 sec64 高 32 位恒 0 ⇒ offset4 处的 i32 恒 0；
       * 8B 的 nsec32 几乎总非 0。nsec32==0 时按 16B 解（8B 且 nsec 恰为 0
       * 的概率 1/1000，误差可忽略）。
       * 【WAIT_BITSET(op=9) 是绝对超时】Linux 语义：默认单调时钟基准；
       * 带 CLOCK_REALTIME(256) 位则实时时钟基准。此前当相对时长睡 ⇒
       * futex_wait_bitset01 睡了"绝对秒数+100ms"（实测 2581ms）。 */
      i32 sec32 = *(volatile i32*)timeout;
      i32 nsec32 = *((volatile i32*)timeout + 1);
      u64 tmo_ms;
      /* 毫秒向上取整：floor 会比 deadline 早醒 1 tick 内的纳秒零头
       * （futex_wait_bitset01 实测 99.0ms < 100.01ms 判 premature） */
      if (nsec32 != 0) {
        tmo_ms = (u64)(u32)sec32 * 1000ull + ((u32)nsec32 + 999999ull) / 1000000ull;
      } else {
        i64 sec64 = *(volatile i64*)timeout;
        i64 nsec64 = *(volatile i64*)((char*)timeout + 8);
        tmo_ms = (u64)(sec64 > 0 ? sec64 : 0) * 1000ull +
                 ((u64)(nsec64 > 0 ? nsec64 : 0) + 999999ull) / 1000000ull;
      }
      if (is_bitset /* FUTEX_WAIT_BITSET: 绝对超时 */) {
        extern u64 schedule_get_ticks();
        u64 now_ms;
        if ((futex_op & 256u) /* FUTEX_CLOCK_REALTIME */) {
          time_t rtc = 0;
          sys_time(&rtc);
          now_ms = (u64)rtc * 1000ull +
                   (u64)(schedule_get_ticks() % (u64)SCHEDULE_FREQUENCY);
        } else {
          now_ms = (u64)schedule_get_ticks(); /* 单调 ms，与 musl 同基准 */
        }
        /* 【+1 补偿记账差一】schedule_state 的递减在睡下去的当个 tick 就
         * 发生 ⇒ 实际睡眠 = sleep_ticks - 1 tick（futex_wait_bitset01 实测
         * 100000us < 100010us 判 premature）。绝对 deadline 少睡不可接受。 */
        sleep_ticks = tmo_ms > now_ms ? (u32)(tmo_ms - now_ms) + 1 : 1;
      } else {
        /* 普通 FUTEX_WAIT：相对时长（musl 内部已折算） */
        sleep_ticks = (u32)tmo_ms;
      }
      if (sleep_ticks == 0) {
        sleep_ticks = 1;
      }
      if (sleep_ticks > 0x40000000u) {
        sleep_ticks = 0x40000000u; /* 防溢出 int */
      }
      has_timeout = 1;
    }
    futex_waiter_add(uaddr, current);
    if (has_timeout) {
      current->sleep_counter = (int)sleep_ticks; /* 到期由 tick 回填 ETIMEDOUT */
    }
    current->state = THREAD_WAITING;
    schedule_switch();
    return 0; /* 【不可达】唤醒后从保存帧回用户态，不会回到这里 */
  }

  if ((fop & 1u) != 0 /* FUTEX_WAKE / FUTEX_WAKE_BITSET */) {
    /* 唤醒登记在该地址上的等待者（不是调用者自己 ✗），val=最多唤醒几个 */
    return (int)futex_waiter_wake(uaddr, val);
  }

  log_debug("sys futext not impl %d\n", futex_op);
  return 0;
}

/* 【内核侧线程退出唤醒】Linux 语义：线程退出时 mm_release 写
 * *clear_child_tid=0 并 futex_wake 该地址。musl 1.2 的线程链表锁
 * （__thread_list_lock）靠这一步释放——退出线程故意带着 tl_lock 走到
 * SYS_exit，由内核解锁；只写不唤醒 ⇒ 所有等锁线程（含 joiner）永久
 * 阻塞（futex_wake02 的 join 阶段实测全灭）。 */
int futex_wake_kernel_userspace(u32* addr, u32 nwake) {
  if (addr == NULL || futex_waiters == NULL) {
    return 0;
  }
  return (int)futex_waiter_wake(addr, nwake);
}
