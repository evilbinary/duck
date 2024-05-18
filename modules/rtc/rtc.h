/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef RTC_H
#define RTC_H

#include "kernel/kernel.h"

#define RTC_ADDR 0x70
#define RTC_DATA 0x71

#ifndef RTC_TIME
#define RTC_TIME
typedef struct rtc_time {
  u32 second;
  u32 minute;
  u32 hour;
  u32 day;
  u32 month;
  u32 year;
} rtc_time_t;
#endif

#endif