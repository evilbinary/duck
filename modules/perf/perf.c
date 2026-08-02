/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "perf.h"

#include "kernel/schedule.h"
#include "kernel/thread.h"
#include "../backtrace/backtrace.h"
#include "../posix/sysfn_no.h"

extern interrupt_handler_t* exception_handlers[];

static perf_stats_t perf_stats;
static interrupt_handler_t perf_origin_timer = NULL;
/* Duration expired in IRQ: stop sampling only; user must SYS_PERF_STOP to dump. */
static volatile u32 perf_dump_pending;

/* 函数级聚合统计（dump 时填充） */
#define PERF_FUNC_MAX 256

typedef struct perf_func_stat {
  char name[BT_SYM_NAME_MAX];
  u32 count;
} perf_func_stat_t;

static perf_func_stat_t perf_funcs[PERF_FUNC_MAX];
static u32 perf_func_count;
static int perf_target_tid = -1;
static char perf_target_name[64];

/* 线程(进程)级概览表：所有进程各占多少采样/时间 */
#define PERF_TID_MAX 128

typedef struct perf_tid_stat {
  int tid;
  u32 count;
} perf_tid_stat_t;

static perf_tid_stat_t perf_tids[PERF_TID_MAX];
static u32 perf_tid_count;

/* 解析 pc 为符号：用户 ELF 优先；失败且地址非用户区回退内核符号表 */
static void perf_sym_resolve(const perf_sample_t* s, char* out, u32 out_size) {
  thread_t* t = NULL;
  if (out_size == 0) {
    return;
  }
  out[0] = 0;
  if (s->mode == 3) {
    t = thread_find_id(s->tid);
    if (t != NULL) {
      bt_sym_lookup(t, s->pc, 3, out, out_size);
    }
  }
  /* 内核线程以 USER level 跑内核代码时 CPSR=USR，用户符号查不到
   * （线程名 "init" 等磁盘上不存在）；地址不在用户区则回退内核符号表
   * （/kernel.elf 已缓存，纯内存查询，不会刷 VFS 错误）。 */
  if (out[0] == 0 && s->pc < EXEC_ADDR) {
    bt_sym_lookup(t, s->pc, 0, out, out_size);
  }
}

static u32 perf_hash_key(int tid, u32 pc, u8 mode) {
  return ((u32)tid * 31u + (pc >> 2) + (u32)mode * 1009u) % PERF_SAMPLES_MAX;
}

/* 采样时刻的 CPU 模式：0=kernel 3=user（与 backtrace 模块约定一致） */
static u8 perf_ic_mode(interrupt_context_t* ic) {
#if defined(X86)
  return (u8)GET_CPL(ic->cs);
#elif defined(ARMV5) || defined(ARMV7) || defined(ARMV7_A)
  return ((ic->psr & 0x1f) == 0x10) ? 3 : 0; /* CPSR M=USR */
#elif defined(ARMV8_A) || defined(ARM64) || defined(__aarch64__)
  return (((u32)ic->psr & 0xf) == 0) ? 3 : 0; /* SPSR_EL1 EL0=00 */
#elif defined(RISCV)
  return ((ic->sstatus & (1u << 8)) == 0) ? 3 : 0; /* SPP=0 -> U-mode */
#else
  return 0;
#endif
}

static u32 perf_ic_pc(interrupt_context_t* ic) {
#if defined(X86)
  return ic->eip;
#elif defined(RISCV)
  return ic->sepc;
#else
  return ic->pc;
#endif
}

static void perf_record(int tid, u32 pc, u8 mode) {
  u32 i = perf_hash_key(tid, pc, mode);
  while (perf_stats.samples[i].count > 0) {
    if (perf_stats.samples[i].tid == tid &&
        perf_stats.samples[i].pc == pc &&
        perf_stats.samples[i].mode == mode) {
      perf_stats.samples[i].count++;
      perf_stats.total++;
      return;
    }
    i = (i + 1) % PERF_SAMPLES_MAX;
  }
  if (perf_stats.count < PERF_SAMPLES_MAX) {
    perf_stats.samples[i].tid = tid;
    perf_stats.samples[i].pc = pc;
    perf_stats.samples[i].mode = mode;
    perf_stats.samples[i].count = 1;
    perf_stats.count++;
    perf_stats.total++;
  } else {
    perf_stats.missed++;
    perf_stats.total++;
  }
}

static void* perf_tick(interrupt_context_t* ic) {
  if (perf_stats.on) {
    if (perf_stats.duration > 0 &&
        schedule_get_ticks() - perf_stats.start_ticks >= perf_stats.duration) {
      /* Never dump from IRQ: AP IRQ stacks are tiny; vfs/sym lookup blows them. */
      perf_stats.on = 0;
      perf_stats.stop_ticks = schedule_get_ticks();
      perf_dump_pending = 1;
    } else {
      perf_stats.div_count++;
      if (perf_stats.div_count >= perf_stats.div) {
        perf_stats.div_count = 0;
        thread_t* cur = thread_current();
        if (cur != NULL && cur->id >= 0) {
          perf_record(cur->id, perf_ic_pc(ic), perf_ic_mode(ic));
        }
      }
    }
  }
  if (perf_origin_timer != NULL) {
    return perf_origin_timer(ic);
  }
  return ic;
}

void perf_start(u32 freq_hz, u32 duration_ticks) {
  if (freq_hz == 0 || freq_hz > PERF_FREQ_DEFAULT) {
    freq_hz = PERF_FREQ_DEFAULT;
  }
  perf_stats.div = PERF_FREQ_DEFAULT / freq_hz;
  if (perf_stats.div == 0) {
    perf_stats.div = 1;
  }
  perf_stats.div_count = 0;
  perf_stats.total = 0;
  perf_stats.missed = 0;
  perf_stats.count = 0;
  perf_stats.duration = duration_ticks;
  if (perf_stats.samples != NULL) {
    kmemset(perf_stats.samples, 0,
            PERF_SAMPLES_MAX * sizeof(perf_sample_t));
  }
  /* 记录被采样线程（调用者，pid==tid），dump 时按 tid 过滤输出函数级统计 */
  thread_t* cur = thread_current();
  perf_target_tid = cur != NULL ? (int)cur->id : -1;
  perf_target_name[0] = 0;
  if (cur != NULL && cur->name != NULL && bt_name_valid(cur, cur->name)) {
    kstrncpy(perf_target_name, cur->name, sizeof(perf_target_name) - 1);
    perf_target_name[sizeof(perf_target_name) - 1] = 0;
  }
  perf_stats.start_ticks = schedule_get_ticks();
  perf_dump_pending = 0;
  perf_stats.on = 1;
  log_info("perf start freq %d hz duration %d ticks tid %d\n", freq_hz,
           duration_ticks, perf_target_tid);
}

typedef struct perf_dump_args {
  u32 min_count;
} perf_dump_args_t;

/* 用 backtrace 符号表把 pc 解析成 函数名+偏移，失败则退回裸地址 */
static void perf_print_sample(const perf_sample_t* s) {
  char sym[BT_SYM_NAME_MAX];
  thread_t* t = thread_find_id(s->tid);
  const char* name =
      (t != NULL && t->name != NULL && bt_name_valid(t, t->name)) ? t->name
                                                                  : "?";
  int pid = t != NULL ? (int)t->pid : -1;
  perf_sym_resolve(s, sym, sizeof(sym));
  u32 pct = perf_stats.total > 0
                ? (u32)((u64)s->count * 100u / perf_stats.total)
                : 0;
  if (sym[0] != 0) {
    kprintf("perf hit: tid %d pid %d %-20s %s count %d (%d%%)\n", s->tid,
            pid, name, sym, s->count, pct);
  } else {
    kprintf("perf hit: tid %d pid %d %-20s pc %x count %d (%d%%)\n",
            s->tid, pid, name, s->pc, s->count, pct);
  }
}

static void perf_dump_syms(void* arg) {
  const perf_dump_args_t* a = (const perf_dump_args_t*)arg;
  for (u32 i = 0; i < PERF_SAMPLES_MAX; i++) {
    if (perf_stats.samples[i].count >= a->min_count) {
      perf_print_sample(&perf_stats.samples[i]);
    }
  }
}

static void perf_dump_top(u32 threshold_pct) {
  if (perf_stats.count == 0) {
    kprintf("perf dump: no samples\n");
    return;
  }
  kprintf("perf dump: %d samples, %d entries, %d missed, cutoff %d%%\n",
          perf_stats.total, perf_stats.count, perf_stats.missed,
          threshold_pct);
  u32 min_count = 1;
  if (perf_stats.total > 0 && threshold_pct > 0) {
    /* threshold_pct 是百分比：保留占比 >= threshold_pct 的 entry。
     * 不能写成 total/threshold_pct（total=39926, cutoff=2 会得到 19963
     * =50% 阈值，导致什么都不输出）。 */
    min_count = (perf_stats.total * threshold_pct) / 100;
  }
  /* 符号化会走 vfs/fatfs 深调用链，切到专用大栈执行（同 fault backtrace） */
  perf_dump_args_t args = {.min_count = min_count};
  bt_run_on_dump_stack(perf_dump_syms, &args);
}

typedef struct perf_func_dump_args {
  int tid;          /* <0 表示不过滤 */
  u32 threshold_pct; /* 0 打印全部 */
} perf_func_dump_args_t;

/* 按符号聚合指定线程的采样：每个函数 耗时(按采样占比折算)/次数，
 * cpu% = 占整个内核采样比例，tid% = 占该线程自身采样比例。
 * 低于阈值的条目直接跳过、不做符号化，避免全量解析符号表太慢。 */
static void perf_dump_funcs_inner(void* arg) {
  const perf_func_dump_args_t* a = (const perf_func_dump_args_t*)arg;
  u32 total = perf_stats.total > 0 ? perf_stats.total : 1;
  u32 duration_ms = perf_stats.stop_ticks > perf_stats.start_ticks
                        ? perf_stats.stop_ticks - perf_stats.start_ticks
                        : 0;

  /* 目标线程自身样本总数（tid% 的分母）；tid<0 时等于全局 */
  u32 tid_total = 0;
  if (a->tid >= 0) {
    for (u32 i = 0; i < PERF_SAMPLES_MAX; i++) {
      const perf_sample_t* s = &perf_stats.samples[i];
      if (s->count > 0 && s->tid == a->tid) {
        tid_total += s->count;
      }
    }
  } else {
    tid_total = total;
  }
  if (tid_total == 0) {
    tid_total = 1;
  }

  /* 阈值按统计范围（过滤后）计算，低于阈值跳过、不做符号化 */
  u32 scope_total = a->tid >= 0 ? tid_total : total;
  u32 min_count = 1;
  if (a->threshold_pct > 0) {
    min_count = (scope_total * a->threshold_pct) / 100;
    if (min_count < 1) {
      min_count = 1;
    }
  }
  char sym[BT_SYM_NAME_MAX];
  u32 truncated = 0;

  perf_func_count = 0;
  for (u32 i = 0; i < PERF_SAMPLES_MAX; i++) {
    const perf_sample_t* s = &perf_stats.samples[i];
    if (s->count == 0) {
      continue;
    }
    if (a->tid >= 0 && s->tid != a->tid) {
      continue;
    }
    if (s->count < min_count) {
      continue; /* 低于阈值：跳过，不做符号化 */
    }

    perf_sym_resolve(s, sym, sizeof(sym));
    if (sym[0] == 0) {
      extern void sprintf(char* buf, const char* fmt, ...);
      sprintf(sym, "pc %x", s->pc);
    }

    u32 idx = PERF_FUNC_MAX;
    for (u32 j = 0; j < perf_func_count; j++) {
      if (kstrcmp(perf_funcs[j].name, sym) == 0) {
        idx = j;
        break;
      }
    }
    if (idx == PERF_FUNC_MAX) {
      if (perf_func_count >= PERF_FUNC_MAX) {
        truncated = 1;
        continue;
      }
      idx = perf_func_count++;
      kstrncpy(perf_funcs[idx].name, sym, sizeof(perf_funcs[idx].name) - 1);
      perf_funcs[idx].name[sizeof(perf_funcs[idx].name) - 1] = 0;
      perf_funcs[idx].count = 0;
    }
    perf_funcs[idx].count += s->count;
  }

  /* 按次数降序（插入排序） */
  for (u32 i = 1; i < perf_func_count; i++) {
    perf_func_stat_t key = perf_funcs[i];
    int j = (int)i - 1;
    while (j >= 0 && perf_funcs[j].count < key.count) {
      perf_funcs[j + 1] = perf_funcs[j];
      j--;
    }
    perf_funcs[j + 1] = key;
  }

  if (a->tid >= 0) {
    kprintf("perf func: tid %d %s samples %d/%d cpu%% %d%% %dms\n", a->tid,
            perf_target_name[0] != 0 ? perf_target_name : "?", tid_total,
            total, (u32)((u64)tid_total * 100u / total), duration_ms);
  } else {
    kprintf("perf func: all samples %d %dms\n", total, duration_ms);
  }
  kprintf("  %-40s %8s %10s %6s %6s\n", "func", "count", "time(ms)",
          "cpu%", "tid%");
  for (u32 i = 0; i < perf_func_count; i++) {
    u32 time_ms = duration_ms > 0
                      ? (u32)((u64)duration_ms * perf_funcs[i].count / total)
                      : 0;
    u32 cpu_pct = (u32)((u64)perf_funcs[i].count * 100u / total);
    u32 tid_pct = (u32)((u64)perf_funcs[i].count * 100u / tid_total);
    kprintf("  %-40s %8d %10d %5d%% %5d%%\n", perf_funcs[i].name,
            perf_funcs[i].count, time_ms, cpu_pct, tid_pct);
  }
  if (truncated) {
    kprintf("  ... (perf func table full, max %d entries)\n", PERF_FUNC_MAX);
  }
}

/* 函数级统计：tid<0 全部线程，threshold_pct=0 打印全部函数 */
static void perf_dump_funcs(int tid, u32 threshold_pct) {
  if (perf_stats.count == 0) {
    return;
  }
  perf_func_dump_args_t args = {.tid = tid, .threshold_pct = threshold_pct};
  bt_run_on_dump_stack(perf_dump_funcs_inner, &args);
}

/* 所有进程(线程)概览：每个 tid 的总采样数/耗时/占比，按采样降序 */
static void perf_dump_tids(void) {
  if (perf_stats.count == 0) {
    return;
  }
  u32 total = perf_stats.total > 0 ? perf_stats.total : 1;
  u32 duration_ms = perf_stats.stop_ticks > perf_stats.start_ticks
                        ? perf_stats.stop_ticks - perf_stats.start_ticks
                        : 0;

  perf_tid_count = 0;
  for (u32 i = 0; i < PERF_SAMPLES_MAX; i++) {
    const perf_sample_t* s = &perf_stats.samples[i];
    if (s->count == 0) {
      continue;
    }
    u32 idx = PERF_TID_MAX;
    for (u32 j = 0; j < perf_tid_count; j++) {
      if (perf_tids[j].tid == s->tid) {
        idx = j;
        break;
      }
    }
    if (idx == PERF_TID_MAX) {
      if (perf_tid_count >= PERF_TID_MAX) {
        continue;
      }
      idx = perf_tid_count++;
      perf_tids[idx].tid = s->tid;
      perf_tids[idx].count = 0;
    }
    perf_tids[idx].count += s->count;
  }

  /* 按采样数降序（插入排序） */
  for (u32 i = 1; i < perf_tid_count; i++) {
    perf_tid_stat_t key = perf_tids[i];
    int j = (int)i - 1;
    while (j >= 0 && perf_tids[j].count < key.count) {
      perf_tids[j + 1] = perf_tids[j];
      j--;
    }
    perf_tids[j + 1] = key;
  }

  kprintf("perf proc: total %d samples %dms\n", total, duration_ms);
  kprintf("  %-8s %-20s %8s %10s %6s\n", "tid", "name", "count",
          "time(ms)", "pct");
  for (u32 i = 0; i < perf_tid_count; i++) {
    thread_t* t = thread_find_id(perf_tids[i].tid);
    const char* name = (t != NULL && t->name != NULL &&
                        bt_name_valid(t, t->name))
                           ? t->name
                           : "?";
    u32 time_ms = duration_ms > 0
                      ? (u32)((u64)duration_ms * perf_tids[i].count / total)
                      : 0;
    u32 pct = (u32)((u64)perf_tids[i].count * 100u / total);
    kprintf("  %-8d %-20s %8d %10d %5d%%\n", perf_tids[i].tid, name,
            perf_tids[i].count, time_ms, pct);
  }
}

void perf_stop(void) {
  u32 was_on = perf_stats.on;
  u32 pending = perf_dump_pending;

  perf_stats.on = 0;
  perf_dump_pending = 0;
  if (!was_on && !pending) {
    return;
  }
  if (was_on) {
    perf_stats.stop_ticks = schedule_get_ticks();
  }
  log_info("perf stop total %d entries %d missed %d\n", perf_stats.total,
           perf_stats.count, perf_stats.missed);
  perf_dump_top(2);
  /* Dump from syscall/thread context only (IRQ only sets perf_dump_pending). */
  perf_dump_tids();
  perf_dump_funcs(perf_target_tid, 1);
}

u32 perf_read(void* buf, u32 size) {
  if (buf == NULL || size < sizeof(perf_sample_t)) {
    return 0;
  }
  u32 copy = size / sizeof(perf_sample_t);
  if (copy > perf_stats.count) {
    copy = perf_stats.count;
  }
  if (copy > 0) {
    kmemcpy(buf, perf_stats.samples, copy * sizeof(perf_sample_t));
  }
  return copy;
}

static u32 sys_perf_start(u32 freq_hz, u32 duration_ticks, u32 arg3, u32 arg4,
                          u32 arg5, u32 arg6, u32 arg7) {
  perf_start(freq_hz, duration_ticks);
  return 0;
}

static u32 sys_perf_stop(u32 arg1, u32 arg2, u32 arg3, u32 arg4, u32 arg5,
                         u32 arg6, u32 arg7) {
  perf_stop();
  return perf_stats.total;
}

static u32 sys_perf_read(u32 buf, u32 size, u32 arg3, u32 arg4, u32 arg5,
                         u32 arg6, u32 arg7) {
  return perf_read((void*)buf, size);
}

void perf_init_syscall(void** syscall_table) {
  syscall_table[SYS_PERF_START] = &sys_perf_start;
  syscall_table[SYS_PERF_STOP] = &sys_perf_stop;
  syscall_table[SYS_PERF_READ] = &sys_perf_read;
}

int perf_init(void) {
  perf_stats.samples =
      (perf_sample_t*)kmalloc(PERF_SAMPLES_MAX * sizeof(perf_sample_t),
                              KERNEL_TYPE);
  if (perf_stats.samples == NULL) {
    log_error("perf init: kmalloc samples failed\n");
    return -1;
  }
  perf_origin_timer = exception_handlers[EX_TIMER];
  exception_regist(EX_TIMER, perf_tick);
  log_info("perf init, samples %x origin timer handler %x\n",
           perf_stats.samples, perf_origin_timer);
  return 0;
}

void perf_exit(void) {
  perf_stop();
  if (perf_stats.samples != NULL) {
    kfree(perf_stats.samples);
    perf_stats.samples = NULL;
  }
  if (perf_origin_timer != NULL) {
    exception_regist(EX_TIMER, perf_origin_timer);
  }
  kprintf("perf exit\n");
}

module_t perf_module = {
    .name = "perf", .init = perf_init, .exit = perf_exit};
