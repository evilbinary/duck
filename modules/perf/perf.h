/*******************************************************************
* Copyright 2021-present evilbinary
* 作者: evilbinary on 01/01/20
* 邮箱: rootdebug@163.com
********************************************************************/
#ifndef PERF_H
#define PERF_H

#include "kernel/kernel.h"

#define PERF_SAMPLES_MAX 32768
#define PERF_FREQ_DEFAULT 1000

typedef struct perf_sample {
  int tid;
  u32 pc;
  u32 count;
} perf_sample_t;

typedef struct perf_stats {
  volatile u32 on;
  u32 div;
  u32 div_count;
  u32 total;
  u32 missed;
  u32 count;
  u32 start_ticks;
  u32 stop_ticks;
  perf_sample_t* samples;
} perf_stats_t;

void perf_start(u32 freq_hz);
void perf_stop(void);
u32 perf_read(void* buf, u32 size);
void perf_init_syscall(void** syscall_table);

#endif
