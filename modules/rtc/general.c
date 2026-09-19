/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
/* 通用 RTC 实现（平台没有专用 RTC 时使用，例如 miyoo）。
 *
 * 【为什么需要】`app/init/module.c` 是**无条件** REGISTER_MODULE(rtc) 的，
 * 而 `duck/modules/rtc/ya.py` 的 plat_source 只给部分平台列了源文件
 * （v3s/raspi2/raspi3/rk3128/stm32/general/t113-s3/versatilepb）⇒ 未列出的
 * 平台（如 miyoo）会编出**空库**，链接期直接
 * `undefined reference to 'rtc_module'`。
 *
 * 【做法】不依赖任何平台寄存器：时间由调度器 tick 换算（自启动起算、
 * 单调递增；**不是**带电池的墙上时间）。接口与 pl031.c / v3s.c 保持一致，
 * 因此 /dev/rtc、/dev/time 在缺失真实 RTC 的平台上依然可用（读数随 tick 前进）。
 * 若某平台接入真实 RTC，只需在 ya.py 的 plat_source 里给它加上专用源文件，
 * 本兜底实现就不会再被选中。
 */
#include "dev/devfs.h"
#include "kernel/kernel.h"
#include "rtc.h"

static rtc_time_t rtc_time;

u32 rtc_is_updating() { return 0; }

void rtc_get_time() {
  int tick = (schedule_get_ticks() / SCHEDULE_FREQUENCY);

  seconds_to_date(tick, &rtc_time.year, &rtc_time.month, &rtc_time.day,
                  &rtc_time.hour, &rtc_time.minute, &rtc_time.second);
}

void rtc_write_time(rtc_time_t* current) {
  for (; rtc_is_updating();) {
  }
}

static size_t rtc_read(device_t* dev, void* buf, size_t len) {
  rtc_time_t* rtc = buf;
  rtc_get_time();
  *rtc = rtc_time;
  return sizeof(rtc_time_t);
}

static size_t rtc_write(device_t* dev, void* buf, size_t len) {
  rtc_time_t* rtc = buf;
  rtc_write_time(rtc);
  return len;
}

int rtc_init(void) {
  device_t* dev = kmalloc(sizeof(device_t), DEFAULT_TYPE);
  dev->name = "rtc";
  dev->read = rtc_read;
  dev->write = rtc_write;
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
