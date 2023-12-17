/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "dev/devfs.h"
#include "serial.h"
#include "gpio.h"

void serial_write(char a) { uart_send(a); }

char serial_read() {

  unsigned int c = 0;  
  if ((UART_REG8(UART_LSR) & UART_LSR_DR)){
    c = (char)(UART_REG8(UART_TX) & 0xff);
  }
  return c;
}

static size_t read(device_t* dev, char* buf, size_t len) {
  // log_debug("read %x %d\n",buf,len);

  u32 count = 0;
  int ret = -1;
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
  log_debug("write %x %d\n",buf,len);
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
