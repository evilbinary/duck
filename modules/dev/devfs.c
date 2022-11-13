/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "devfs.h"
#include "pty/pty.h"

voperator_t no_rw_operator = {.close = device_close,
                              .read = device_read,
                              .write = device_write,
                              .open = vfs_open,
                              .find = vfs_find,
                              .mount = vfs_mount,
                              .readdir = vfs_readdir};

voperator_t device_operator = {.ioctl = device_ioctl,
                               .close = device_close,
                               .open = device_open,
                               .read = device_read,
                               .write = device_write,
                               .find = vfs_find,
                               .mount = vfs_mount,
                               .readdir = vfs_readdir};

vnode_t *devfs_create_device(device_t *dev) {
  vnode_t *t = vfs_create_node("dev", V_DIRECTORY);
  t->flags = V_BLOCKDEVICE | V_DIRECTORY;
  t->op = &no_rw_operator;
  t->device = dev;
  return t;
}

int devfs_init(void) {
  vnode_t *node_dev = vfs_create_node("dev", V_DIRECTORY);
  vfs_mount(NULL, "/", node_dev);

  // SYS_READ,SYS_WRITE
  vnode_t *stdin = vfs_create_node("stdin", V_FILE);
  vnode_t *stdout = vfs_create_node("stdout", V_FILE);
  vnode_t *stderr = vfs_create_node("stderr", V_FILE);
  vfs_mount(NULL, "/dev", stdin);
  vfs_mount(NULL, "/dev", stdout);
  vfs_mount(NULL, "/dev", stderr);

  stdin->op = &device_operator;
  stdout->op = &device_operator;
  stderr->op = &device_operator;

  stdin->device = device_find(DEVICE_KEYBOARD);
  stdout->device = device_find(DEVICE_VGA);

  if (stdout->device == NULL) {
    stdout->device = device_find(DEVICE_VGA_QEMU);
  }
  if (stdout->device == NULL) {
    stdout->device = device_find(DEVICE_SERIAL);
  }
  stderr->device = stdout->device;
  
  fd_std_init();

  //log
  vnode_t *log = vfs_create_node("log", V_FILE | V_BLOCKDEVICE);
  log->device = device_find(DEVICE_LOG);
  log->op = &device_operator;
  vfs_mount(NULL, "/dev", log);

  return 0;
}

void devfs_exit(void) { kprintf("devfs exit\n"); }

module_t devfs_module = {
    .name = "devfs", .init = devfs_init, .exit = devfs_exit};