/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "dev/devfs.h"
#include "gpio.h"
#include "serial.h"
#include "kernel/thread.h"

void serial_write(char a) { uart_send(a); }

char serial_read() {
  if (io_read32(UART0_FR) & 0x10) {
    return 0;
  }
  char c = io_read32(UART0_DR);
  return c;
}

void serial_printf(char* fmt, ...) {
  int i;
  char buf[1024];
  va_list args;
  va_start(args, fmt);
  i = kvsnprintf(buf, sizeof(buf), fmt, &args); /* 有界：以前无界写 1024 字节栈缓冲 */
  va_end(args);
  for (int j = 0; j < i; j++) {
    serial_write(buf[j]);
  }
}

static size_t read(device_t* dev, void* buf, size_t len) {
  u32 count = 0;
  int ret = 0;
  /* 【与 raspi2 保持一致的非阻塞语义】FIFO 空时立刻返回 0，由用户态
   * 重试（shell: getchar 返回 EOF → sleep(100) → 再 getchar；newlib
   * 的 __srefill 不会因 EOF 短路，会重新发起 read）。
   * 【不要在这里自旋等数据】驱动 read 是在 vread 的 vfs 全局锁里执行的，
   * 自旋等待会让 shell 长时间持锁：其它进程（/bin/config）被挂进
   * rt_mutex 等待链；而 rt_mutex_unlock 会把等待者状态强设为 RUNNING
   * （对已 thread_exit 的线程就是从死状态"复活"），引发后续整套
   * 调度/锁连锁异常，表现为输入若干字符后 shell 永久卡死。 */
  if (len == 0) {
    return 0;
  }
  for (u32 i = 0; i < len; i++) {
    char c = serial_read();
    if (c != 0) {
      ((char*)buf)[count++] = c;
      ret = count;
    }
  }
  return ret;
}

static size_t write(device_t* dev, void* buf, size_t len) {
  u32 ret = len;
  for (int i = 0; i < len; i++) {
    serial_write(((char*)buf)[i]);
  }
  return ret;
}

int serial_init(void) {
  device_t* dev = kmalloc(sizeof(device_t), DEFAULT_TYPE);
  dev->name = "serial";
  dev->read = read;
  dev->write = write;
  dev->id = DEVICE_SERIAL;
  dev->type = DEVICE_TYPE_CHAR;
  device_add(dev);

  // series
  vnode_t* series = vfs_create_node("series", V_FILE);
  vfs_mount(NULL, "/dev", series);
  series->device = dev;
  series->op = &device_operator;

  // uart_init();
  return 0;
}

void serial_exit(void) { kprintf("serial exit\n"); }

module_t serial_module = {
    .name = "serial", .init = serial_init, .exit = serial_exit};
