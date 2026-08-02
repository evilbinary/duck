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
      perf_stop();
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
  perf_stats.start_ticks = schedule_get_ticks();
  perf_stats.on = 1;
  log_info("perf start freq %d hz duration %d ticks\n", freq_hz,
           duration_ticks);
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
  int pid = t != NULL ? t->pid : -1;
  sym[0] = 0;
  if (s->mode == 3) {
    if (t != NULL) {
      bt_sym_lookup(t, s->pc, 3, sym, sizeof(sym));
    }
  }
  /* 内核线程以 USER level 跑内核代码时 CPSR=USR，用户符号查不到
   * （线程名 "init" 等磁盘上不存在）；地址不在用户区则回退内核符号表
   * （/kernel.elf 已缓存，纯内存查询，不会刷 VFS 错误）。 */
  if (sym[0] == 0 && s->pc < EXEC_ADDR) {
    bt_sym_lookup(t, s->pc, 0, sym, sizeof(sym));
  }
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

void perf_stop(void) {
  if (!perf_stats.on) {
    return;
  }
  perf_stats.on = 0;
  perf_stats.stop_ticks = schedule_get_ticks();
  log_info("perf stop total %d entries %d missed %d\n", perf_stats.total,
           perf_stats.count, perf_stats.missed);
  perf_dump_top(2);
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
