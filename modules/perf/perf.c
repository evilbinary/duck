/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "perf.h"

#include "kernel/schedule.h"
#include "../posix/sysfn_no.h"

extern interrupt_handler_t* exception_handlers[];

static perf_stats_t perf_stats;
static interrupt_handler_t perf_origin_timer = NULL;

static u32 perf_hash_key(int tid, u32 pc) {
  return ((u32)tid * 31u + (pc >> 2)) % PERF_SAMPLES_MAX;
}

static void perf_record(int tid, u32 pc) {
  u32 i = perf_hash_key(tid, pc);
  while (perf_stats.samples[i].count > 0) {
    if (perf_stats.samples[i].tid == tid &&
        perf_stats.samples[i].pc == pc) {
      perf_stats.samples[i].count++;
      perf_stats.total++;
      return;
    }
    i = (i + 1) % PERF_SAMPLES_MAX;
  }
  if (perf_stats.count < PERF_SAMPLES_MAX) {
    perf_stats.samples[i].tid = tid;
    perf_stats.samples[i].pc = pc;
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
          perf_record(cur->id, ic->pc);
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
    min_count = perf_stats.total / threshold_pct;
  }
  for (u32 i = 0; i < PERF_SAMPLES_MAX; i++) {
    if (perf_stats.samples[i].count >= min_count) {
      kprintf("perf hit: tid %d pc %x count %d (%.1f%%)\n",
              perf_stats.samples[i].tid, perf_stats.samples[i].pc,
              perf_stats.samples[i].count,
              perf_stats.total > 0
                  ? (float)perf_stats.samples[i].count * 100.0f /
                        (float)perf_stats.total
                  : 0.0f);
    }
  }
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
