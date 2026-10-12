/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef DEVICE_H
#define DEVICE_H

#include "kernel/kernel.h"
#include "config.h"

enum {
  DEVICE_KEYBOARD = 0,
  DEVICE_PCI,
  DEVICE_VGA,
  DEVICE_VGA_QEMU,
  DEVICE_VGA_VBOX,
  DEVICE_MOUSE,
  DEVICE_SERIAL,
  DEVICE_RTC,
  DEVICE_SATA,
  DEVICE_SATA0,
  DEVICE_SATA1,
  DEVICE_SATA2,
  DEVICE_SATA3,
  DEVICE_SATA4,
  DEVICE_SATA5,
  DEVICE_SATA6,
  DEVICE_I2C,
  DEVICE_GPIO,
  DEVICE_SPI,
  DEVICE_SB,
  DEVICE_NET,
  DEVICE_LOG,
  DEVICE_NULL,
  DEVICE_TRACE,
  DEVICE_LCD,
  DEVICE_POWER,
  DEVICE_IRQ, /* /dev/irq：统一中断框架的只读诊断出口（modules/irq） */
  DEVICE_JOYSTICK, /* /dev/joystick：GPIO 手柄（miyoomini 等），上报 KEY_* 码流 */
  DEVICE_FAN, /* /dev/fan：Pi5 RP1 PWM1 风扇（modules/fan，1 字节 level） */
  DEVICE_CPU, /* /dev/cpu：固件 mailbox 温度，read 返回 4 字节毫摄氏度 */
};

enum {
  DEVICE_TYPE_CHAR = 1,
  DEVICE_TYPE_BLOCK,
  DEVICE_TYPE_VIRTUAL,
  DEVICE_TYPE_FILE,
  DEVICE_TYPE_VGA,
  DEVICE_TYPE_NET
};

#ifndef MAX_DEVICE
#define MAX_DEVICE 256
#endif

typedef struct dstat {
  size_t size;
  int is_dir;
} dstat_t;

typedef struct device device_t;

typedef size_t (*dev_read_fn)(device_t* dev, void* buf, size_t len);
typedef size_t (*dev_write_fn)(device_t* dev, const void* buf, size_t len);
typedef size_t (*dev_stat_fn)(device_t* dev, dstat_t* stat);
typedef size_t (*dev_ioctl_fn)(device_t* dev, u32 cmd, ...);
typedef void (*device_notify_fn)(device_t* dev);

typedef struct device {
  char* name;
  u32 id;
  u32 type;  // char block vga
  dev_read_fn read;
  dev_write_fn write;
  dev_ioctl_fn ioctl;
  dev_stat_fn stat;
  void* data;  // data
  /* 【新字段必须追加在末尾】本构建系统不跟踪头文件依赖：若插在中间，
   * 其它模块（xwin 等）仍按旧偏移读 data ⇒ 会把 data 读成垃圾。
   * index：同一 id 下区分"第几块同类设备"（如 /dev/fb0、/dev/fb1 两块显示
   * 都是 DEVICE_VGA）。单设备默认 0。 */
  u32 index;
} device_t;


void device_add(device_t* device);

void device_set_notify(device_notify_fn fn);

void device_remove();

device_t* device_find(u32 id);

/* 按设备名查找（驱动注册 device_t 时填的 name，如 "vga"/"serial"/"vga1"）。
 * 多实例设备（多块屏/多个串口）用名字定位比用 id 直观。 */
device_t* device_find_name(char* name);

device_t* device_create(u8* name, u32 id, u32 type);

#endif