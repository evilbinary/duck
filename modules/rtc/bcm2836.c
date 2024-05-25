/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "dev/devfs.h"
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



void rtc_get_time() {
  int tick = (schedule_get_ticks() / SCHEDULE_FREQUENCY);
  
  seconds_to_date(tick,&rtc_time.year,&rtc_time.month,&rtc_time.day,
    &rtc_time.hour,&rtc_time.minute,&rtc_time.second);

    kprintf("%d-%d-%d %d:%d:%d\n", rtc_time.year, rtc_time.month, rtc_time.day,
          rtc_time.hour, rtc_time.minute, rtc_time.second);
}

void rtc_write_time(rtc_time_t* current) { for (; rtc_is_updating();); }

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

  return 0;
}

void rtc_exit(void) { kprintf("rtc exit\n"); }

module_t rtc_module = {.name = "rtc", .init = rtc_init, .exit = rtc_exit};
