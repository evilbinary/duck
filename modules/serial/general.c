/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "serial.h"
#include "dev/devfs.h"


void serial_write(char a) {
  putchar(a);
}

char serial_read() {
  return getchar();
}

static size_t read(device_t* dev, void* buf, size_t len) {
  u32 ret = 0;
  for (int i = 0; i < len; i++) {
    ((char*)buf)[i] = serial_read();
    if(((char*)buf)[i]>0){
      ret++;
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
  device_t* dev = kmalloc(sizeof(device_t),DEFAULT_TYPE);
  dev->name = "serial";
  dev->read = read;
  dev->write = write;
  dev->id = DEVICE_SERIAL;
  dev->type = DEVICE_TYPE_CHAR;
  device_add(dev);

  // series
  vnode_t *series = vfs_create_node("series", V_FILE);
  vfs_mount(NULL, "/dev", series);
  series->device = dev;
  series->op = &device_operator;

  return 0;
}

void serial_exit(void) { kprintf("serial exit\n"); }

module_t serial_module = {
    .name = "serial", .init = serial_init, .exit = serial_exit};