/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "dev/devfs.h"
#include "serial.h"

void serial_write(char a) { uart_send(a); }

char serial_read() {
  // return uart_receive();
  unsigned int c = 0;
  unsigned int addr = 0x01c28000;  // UART0
  if ((io_read32(addr + 0x14) & (0x1 << 0)) != 0) {
    c = io_read32(addr + 0x00);
  }
  return c;
}

void serial_printf(char* fmt, ...) {
  int i;
  char buf[1024];
  va_list args;
  va_start(args, fmt);
  i = kvsprintf(buf, fmt, args);
  va_end(args);
  for (int j = 0; j < i; j++) {
    serial_write(buf[j]);
  }
}

static size_t read(device_t* dev, char* buf, size_t len) {
  u32 count = 0;
  int ret = 0;
  for (int i = 0; i < len; i++) {
    char c = serial_read();
    if (c != 0) {
      ((char*)buf)[count++] = c;
      ret = count;
    }
  }
  return ret;
}

static size_t write(device_t* dev, char* buf, size_t len) {
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

  // uart_init();

  // devfs series
  vnode_t* series = vfs_create_node("series", V_FILE);
  vfs_mount(NULL, "/dev", series);
  series->device = dev;
  series->op = &device_operator;

  return 0;
}

void serial_exit(void) { kprintf("serial exit\n"); }

module_t serial_module = {
    .name = "serial", .init = serial_init, .exit = serial_exit};
