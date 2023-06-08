/*******************************************************************
* Copyright 2021-2080 evilbinary
* 作者: evilbinary on 01/01/20
* 邮箱: rootdebug@163.com
********************************************************************/
#include "kernel/kernel.h"
#include "dev/devfs.h"

size_t log_read(device_t* dev, void* buf, size_t len) {
  u32 ret = 0;
  return ret;
}

size_t log_write(device_t* dev, const void* buf, size_t len) {
  u32 ret = 0;
  //log_info(buf);
  return ret;
}


static int log_init(void) {
  device_t* dev = kmalloc(sizeof(device_t),DEFAULT_TYPE);
  dev->name = "log";
  dev->read = log_read;
  dev->write = log_write;
  dev->id = DEVICE_LOG;
  dev->type = DEVICE_TYPE_CHAR;
  device_add(dev);

  //log
  vnode_t *log = vfs_create_node("log", V_FILE | V_BLOCKDEVICE);
  log->device = device_find(DEVICE_LOG);
  log->op = &device_operator;
  vfs_mount(NULL, "/dev", log);

  return 0;
}

void log_exit(void) { kprintf("log exit\n"); }


module_t log_module = {
    .name ="log",
    .init=log_init,
    .exit=log_exit
};
