/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * 从 sysfn.c 拆出：时间（time/clock_gettime/nanosleep/alarm/setitimer）。
 ********************************************************************/
#include "sysfn.h"

#include "kernel/error.h"
#include "kernel/kernel.h"
#include "kernel/schedule.h"
#include "kernel/thread.h"
#include "kernel/time.h"
#include "kernel/vfs.h"

#define log_debug

/* 【alarm 最小语义：登记 + 返回旧剩余秒数】alarm02 验证 alarm(0) 返回旧剩余、
 * alarm03 验证 fork 不继承（子 alarm(0)=0，父 alarm(0)=100）。
 * 【不做到期动作】内核无信号投递设施，到期终止只能在中断上下文走轻量
 * thread_stop 路径，仍有整机自旋风险（adjtimex02 实测）；TCG 下 guest 时钟
 * 比墙钟慢约 60 倍，alarm(30) 看门狗实际是墙钟 30 分钟形同虚设。到期兜底
 * 完全交给外部 runner（墙钟超时 + 杀 QEMU 重启）。
 * 【向上取整】alarm(100) 后 1ms 内 alarm(0) 必须仍回 100（LTP 用整值断言），
 * 整除会得 99，故用 ceil(rem/FREQUENCY)。 */
struct sys_alarm_ent {
  int tid;
  ullong expire; /* 绝对 tick（1 tick = 1ms） */
};
static struct sys_alarm_ent s_alarm_tab[16];
static int s_alarm_n = 0;

unsigned int sys_alarm(unsigned int seconds) {
  thread_t* current = thread_current();
  if (current == NULL) {
    return 0;
  }
  int i;
  int slot = -1;
  /* 【顺带回收死线程条目】测试退出不调 alarm(0)（tst_test 的 watchdog 闹钟
   * 一直挂着），16 槽随测试数积累会满 ⇒ 新 alarm 登记失败。tid 已不存在
   * 即条目作废，lazy 清理（仅在下次 alarm 调用时扫一遍，无中断上下文）。 */
  i = 0;
  while (i < s_alarm_n) {
    if (s_alarm_tab[i].tid != (int)current->id &&
        thread_find_id(s_alarm_tab[i].tid) == NULL) {
      s_alarm_tab[i] = s_alarm_tab[s_alarm_n - 1];
      s_alarm_n--;
      continue;
    }
    if (s_alarm_tab[i].tid == (int)current->id) {
      slot = i;
    }
    i++;
  }
  ullong now = (ullong)schedule_get_ticks();
  /* 旧剩余秒数（未设为 0），向上取整保住整值断言 */
  unsigned int old_left = 0;
  if (slot >= 0) {
    old_left = s_alarm_tab[slot].expire > now
                   ? (unsigned int)((s_alarm_tab[slot].expire - now +
                                     (ullong)SCHEDULE_FREQUENCY - 1) /
                                    (ullong)SCHEDULE_FREQUENCY)
                   : 0;
  }
  if (seconds == 0) {
    if (slot >= 0) {
      s_alarm_tab[slot] = s_alarm_tab[s_alarm_n - 1];
      s_alarm_n--;
    }
    return old_left;
  }
  if (slot < 0 && s_alarm_n < 16) {
    slot = s_alarm_n++;
    s_alarm_tab[slot].tid = (int)current->id;
  }
  if (slot >= 0) {
    s_alarm_tab[slot].expire = now + (ullong)seconds * (ullong)SCHEDULE_FREQUENCY;
  }
  /* 表满：登记失败但语义上仍返回 old_left（超长闹钟本就永不到期） */
  return old_left;
}

/* ===================== setitimer ===================== */

/* 【ITIMER_REAL 必须真实现】aarch64 musl 的 alarm() 就是
 * setitimer(ITIMER_REAL, {sec,0}, &old)（musl src/unistd/alarm.c），此前
 * no-op 恒回 0 ⇒ alarm02/03 全灭。与 sys_alarm 共用同一登记表（每进程一个
 * real-time 定时器语义一致）。仍无到期动作（无信号基建），兜底靠外部 runner。
 * struct itimerval = 2×struct timeval{long sec; long usec} = 32B：
 *   [0..7]=interval.sec [8..15]=interval.usec [16..23]=value.sec [24..31]=value.usec
 * （Linux 语义 interval 在前；探针实测 musl alarm(2147483647) 时 offset0 恒 0、
 *  offset16 才是 2147483647，证实 value 在后半。） */
int sys_setitimer(int which, void* new_val, void* old_val) {
  if (which != 0 /* ITIMER_REAL */) {
    /* VIRTUAL/PROF 无记账，恒"未设置" */
    if (old_val != NULL) {
      kmemset(old_val, 0, 32);
    }
    return 0;
  }
  thread_t* current = thread_current();
  if (current == NULL) {
    return -1;
  }
  /* old：当前登记的剩余秒数（ceil），value.usec=0 ⇒ musl alarm 恰好取整值 */
  if (old_val != NULL) {
    kmemset(old_val, 0, 32);
    int slot = -1;
    for (int i = 0; i < s_alarm_n; i++) {
      if (s_alarm_tab[i].tid == (int)current->id) {
        slot = i;
        break;
      }
    }
    if (slot >= 0) {
      ullong now = (ullong)schedule_get_ticks();
      if (s_alarm_tab[slot].expire > now) {
        ullong rem = s_alarm_tab[slot].expire - now;
        long left = (long)((rem + (ullong)SCHEDULE_FREQUENCY - 1) /
                           (ullong)SCHEDULE_FREQUENCY);
        /* 写 value.sec（offset 16），interval 段保持 0 —— musl alarm 读
         * old.it_value.tv_sec + !!old.it_value.tv_usec，恰好整值 */
        *((long*)old_val + 2) = left;
      }
    }
  }
  long sec = 0;
  long usec = 0;
  if (new_val != NULL) {
    sec = *((long*)new_val + 2); /* value.sec 在 offset 16（interval 在前） */
    usec = *((long*)new_val + 3);
  }
  int slot = -1;
  for (int i = 0; i < s_alarm_n; i++) {
    if (s_alarm_tab[i].tid == (int)current->id) {
      slot = i;
      break;
    }
  }
  if (sec == 0 && usec == 0) {
    /* 取消 */
    if (slot >= 0) {
      s_alarm_tab[slot] = s_alarm_tab[s_alarm_n - 1];
      s_alarm_n--;
    }
    return 0;
  }
  if (slot < 0 && s_alarm_n < 16) {
    slot = s_alarm_n++;
    s_alarm_tab[slot].tid = (int)current->id;
  }
  if (slot >= 0) {
    ullong now = (ullong)schedule_get_ticks();
    s_alarm_tab[slot].expire = now + (ullong)sec * (ullong)SCHEDULE_FREQUENCY +
                               (ullong)usec / 1000ull;
  }
  return 0;
}

/* ===================== sleep ===================== */

/* 【重要·应用其实走的是这一路】musl 的 __clock_nanosleep 在本平台
 * （SYS_clock_nanosleep(230) != SYS_clock_nanosleep_time64(407)）且 tv_sec 为
 * 小值时，会**跳过 407 分支**、回落到 __syscall_cp(SYS_nanosleep) —— 也就是
 * 系统调用 162，进到这里。排查睡眠相关问题时务必同时覆盖这一路
 * （曾因只改 sys_clock_nanosleep 而得出"应用没发请求"的错误结论）。 */
int sys_nanosleep(struct timespec* req, struct timespec* rem) {
  if (req == NULL) {
    return -1;
  }
  {
    /* YiYiYa·保险丝（治标但立刻见效）：用户态有路径在传 ~1.6-1.9 秒的延时
     * （来源未定 ✗），一睡就把整个 UI 冻住、手柄像没反应 ✗。这里把【任何】
     * >100ms 的单次睡眠夹到 100ms：语义上只是"更频繁地醒来重新请求"（轮询 ✓），
     * 不会漏事件 ✓，也不会再出现 1.9 秒的大冻 ✓。 */
    u32 tick = (u32)(SECOND_TO_TICK(req->tv_sec) +
                     NANOSECOND_TO_TICK(req->tv_nsec));
    if (tick > 100) {
      tick = 100;
    }
    schedule_sleep(tick);
  }
  return 0;
}

int sys_clock_nanosleep(int clock, int flag, struct timespec* req,
                        struct timespec* rem) {
  if (req == NULL) {
    return -1;
  }
  {
    /* YiYiYa·保险丝（治标但立刻见效）：用户态有路径在传 ~1.6-1.9 秒的延时
     * （来源未定 ✗），一睡就把整个 UI 冻住、手柄像没反应 ✗。这里把【任何】
     * >100ms 的单次睡眠夹到 100ms：语义上只是"更频繁地醒来重新请求"（轮询 ✓），
     * 不会漏事件 ✓，也不会再出现 1.9 秒的大冻 ✓。 */
    u32 tick = (u32)(SECOND_TO_TICK(req->tv_sec) +
                     NANOSECOND_TO_TICK(req->tv_nsec));
    if (tick > 100) {
      tick = 100;
    }
    schedule_sleep(tick);
  }
  return 0;
}

/* ===================== time / clock ===================== */

u32 sys_time(time_t* t) {
  vnode_t* time_node = vfs_find(NULL, "/dev/time");
  rtc_time_t time;
  time_t seconds = 0;
  time.day = 1;
  time.hour = 0;
  time.minute = 0;
  time.month = 1;
  time.second = 0;
  time.year = 1970;

  if (t == NULL) {
    return -1;
  }
  if (time_node == NULL) {
    log_error("cannot found file /dev/time\n");
    kmemcpy(t, &seconds, sizeof(time_t));
    return 0;
  }

  u32 ret = vread(time_node, 0, sizeof(rtc_time_t), &time);
  if (ret < 0) {
    return 0;
  }

  seconds = secs_of_years(time.year - 1) + secs_of_month(time.month - 1, time.year) +
            (time.day - 1) * 86400 + time.hour * 3600 + time.minute * 60 +
            time.second + 0;

  kmemcpy(t, &seconds, sizeof(time_t));

  return ret;
}

/* 【time64 布局写入辅助】内核 struct timespec 是 { int64 tv_sec; long tv_nsec; }
 * —— tv_nsec 只有 4 字节（偏移 8，其后 4 字节是填充）。但 musl 的 clock_gettime
 * 按 time64 布局读取 tv_nsec 的【8 字节】，高 4 字节若没写就是调用者栈上的旧数据
 * ⇒ 应用拿到的时间值是乱数。反汇编已证实旧写法只发 4 字节存储：
 *   `strd r2,[r1]`（tv_sec 8 字节）+ `str r2,[r3,#8]`（tv_nsec 只有 4 字节）。
 * 这里显式按 16 字节 time64 布局写满。
 * 注意：不能直接改 struct timespec 本身——struct stat 内嵌了它
 * （st_atim/st_mtim/st_ctim），改布局会破坏 stat 的应用可见 ABI。 */
struct timespec64_layout {
  long long tv_sec;
  long long tv_nsec;
};

static inline void sys_time64_write(struct timespec* ts, long long sec,
                                    long long nsec) {
  struct timespec64_layout* t = (struct timespec64_layout*)ts;
  t->tv_sec = sec;
  t->tv_nsec = nsec;
}

int sys_clock_gettime64(clockid_t clockid, struct timespec* ts) {
  if (ts == NULL) {
    return -1;
  }
  if (clockid == CLOCK_MONOTONIC || clockid == CLOCK_MONOTONIC_RAW ||
      clockid == CLOCK_MONOTONIC_COARSE || clockid == CLOCK_BOOTTIME) {
    /* 单调时钟必须用累计 tick，不能 ticks%1000（会回绕导致 GetTicks 倒退、
     * SDL_Delay/sys_sleep 算出超长睡眠 → 黑屏）。 */
    u64 ticks = schedule_get_ticks();
    /* 【必须按 time64 布局写满 16 字节】见上面 sys_time64_write 的说明：
     * 旧写法只写 4 字节 tv_nsec ⇒ 高 4 字节是栈上垃圾 ⇒ 应用侧的
     * clock_gettime 返回值变成乱数。实测后果：infones 的帧节流用它判断
     * "是否到了下一帧时刻"，比较永远不成立 ⇒ 从不调用 nanosleep ⇒
     * 帧率怎么都降不下来（固定 81fps）。 */
    sys_time64_write(ts, (long long)(ticks / SCHEDULE_FREQUENCY),
                     (long long)((ticks % SCHEDULE_FREQUENCY) *
                                 (1000000000u / SCHEDULE_FREQUENCY)));
    return 0;
  }
  if (clockid == CLOCK_REALTIME || clockid == CLOCK_REALTIME_COARSE) {
    time_t seconds;
    sys_time(&seconds);
    u64 ticks = schedule_get_ticks();
    sys_time64_write(ts, (long long)seconds,
                     (long long)((ticks % SCHEDULE_FREQUENCY) *
                                 (1000000000u / SCHEDULE_FREQUENCY)));
    return 0;
  }
  if (clockid == CLOCK_THREAD_CPUTIME_ID ||
      clockid == CLOCK_PROCESS_CPUTIME_ID) {
    /* 同样必须写满 time64 的 16 字节（旧写法只写 4 字节 tv_nsec ⇒ 高 4 字节
     * 是调用者栈上的旧数据）。 */
    sys_time64_write(ts, 0, 0);
    return 0;
  }
  log_warn("clock not support %d\n", clockid);
  return -1;
}

/* 【clock_getres(114)】aarch64 的 114 是 clock_getres（113 才是 clock_gettime）。
 * 之前没注册：调用方拿到 ENOSYS，部分定时器/休眠实现会直接放弃。返回本内核
 * 的时钟粒度：tick 计时精度即 1ms 级别，但 Linux 惯例是 1ns，调用方只用它
 * 做"最小可睡眠单位"的粗估，写 1ns 更安全（不会把调用方推向毫秒级空转）。 */
int sys_clock_getres(clockid_t clockid, struct timespec* res) {
  (void)clockid;
  if (res == NULL) { /* Linux：NULL 合法（只查询不改值） */
    return 0;
  }
  sys_time64_write(res, 0, 1);
  return 0;
}
