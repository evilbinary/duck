/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "dev/devfs.h"
#include "gpio.h"
#include "kernel/kernel.h"
#include "rtc.h"

rtc_time_t rtc_time;

u32 rtc_is_updating() { return 0; }

void rtc_convert_bcd(rtc_time_t* current, u32 b) {
  if (!(b & 0x04)) {
    current->second = (current->second & 0x0F) + ((current->second / 16) * 10);
    current->minute = (current->minute & 0x0F) + ((current->minute / 16) * 10);
    current->hour =
        ((current->hour & 0x0F) + (((current->hour & 0x70) / 16) * 10)) |
        (current->hour & 0x80);
    current->day = (current->day & 0x0F) + ((current->day / 16) * 10);
    current->month = (current->month & 0x0F) + ((current->month / 16) * 10);
    current->year = (current->year & 0x0F) + ((current->year / 16) * 10);
  }
}

// 判断是否为闰年
int is_leap_year(int year) {
  if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
    return 1;  // 是闰年
  } else {
    return 0;  // 不是闰年
  }
}

// 天数转换为年月日
void convert_days_to_date(int days, int* year, int* month, int* day) {
  *year = 1900;  // 开始年份
  *month = 1;    // 开始月份
  *day = 1;      // 开始日

  if (days < 30) {
    return;
  }

  while (days > 0) {
    // 如果是闰年，二月有29天，否则有28天
    if (is_leap_year(*year)) {
      if (days > 366) {
        days -= 366;
        (*year)++;
      } else {
        *month = 3;
        *day = days - 31;  // 1月31天，2月29天
        days = 0;
      }
    } else {
      // 非闰年的二月有28天
      static const int days_per_month[12] = {31, 28, 31, 30, 31, 30,
                                             31, 31, 30, 31, 30, 31};
      if (days > days_per_month[*month - 1]) {
        days -= days_per_month[*month - 1];
        (*month)++;
      } else {
        *day = days;
        days = 0;
      }
    }
  }
}

void rtc_get_time() {
  for (; rtc_is_updating();)
    ;

  u32 d = io_read32(RTC_BASE + 0x0010);
  u32 t = io_read32(RTC_BASE + 0x0014);

  rtc_time.second = ((t >> 0) & 0x3f);
  rtc_time.minute = ((t >> 8) & 0x3f);
  rtc_time.hour = ((t >> 16) & 0x1f);

  convert_days_to_date(d, &rtc_time.year, &rtc_time.month, &rtc_time.day);

  // kprintf("day %d  t %d\n", d, t);

//   kprintf("%d-%d-%d %d:%d:%d\n", rtc_time.year, rtc_time.month, rtc_time.day,
//           rtc_time.hour, rtc_time.minute, rtc_time.second);
}

void rtc_write_time(rtc_time_t* current) {
  for (; rtc_is_updating();)
    ;
}

static size_t read(device_t* dev, void* buf, size_t len) {
  u32 ret = 0;
  rtc_get_time();

  rtc_time_t* rtc = buf;
  *rtc = rtc_time;

  return ret;
}

static size_t write(device_t* dev, void* buf, size_t len) {
  u32 ret = len;
  rtc_time_t* rtc = buf;
  rtc_write_time(rtc);
  return ret;
}

int rtc_init(void) {
  device_t* dev = kmalloc(sizeof(device_t), DEFAULT_TYPE);
  dev->name = "rtc";
  dev->read = read;
  dev->write = write;
  dev->id = DEVICE_RTC;
  dev->type = DEVICE_TYPE_CHAR;
  dev->data = &rtc_time;

  device_add(dev);

  // time
  device_t* rtc_dev = device_find(DEVICE_RTC);
  if (rtc_dev != NULL) {
    vnode_t* time = vfs_create_node("time", V_FILE);
    vfs_mount(NULL, "/dev", time);
    time->device = rtc_dev;
    time->op = &device_operator;
  } else {
    kprintf("dev time not found\n");
  }

  // init
  io_write32(RTC_BASE + 0, (1 << 14) | (1 << 4));
  u32 val = io_read32(RTC_BASE + 0);
  val &= ~(0x3 << 0);
  val |= (0x16aa << 16);
  io_write32(RTC_BASE + 0, val);

  return 0;
}

void rtc_exit(void) { kprintf("rtc exit\n"); }

module_t rtc_module = {.name = "rtc", .init = rtc_init, .exit = rtc_exit};
