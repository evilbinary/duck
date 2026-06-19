/*******************************************************************
 * Copyright 2021-present evilbinary
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

static vnode_t* devfs_stdin;
static vnode_t* devfs_stdout;
static vnode_t* devfs_stderr;

static device_t* devfs_pick_output_device(void) {
  device_t* dev = device_find(DEVICE_VGA);
  if (dev != NULL) {
    return dev;
  }
  dev = device_find(DEVICE_VGA_QEMU);
  if (dev != NULL) {
    return dev;
  }
  dev = device_find(DEVICE_LCD);
  if (dev != NULL) {
    return dev;
  }
  return device_find(DEVICE_SERIAL);
}

static device_t* devfs_pick_input_device(void) {
  device_t* dev = device_find(DEVICE_KEYBOARD);
  if (dev != NULL) {
    return dev;
  }
  return device_find(DEVICE_SERIAL);
}

static void devfs_bind_stdio(void) {
  if (devfs_stdin != NULL) {
    devfs_stdin->device = devfs_pick_input_device();
  }
  if (devfs_stdout != NULL) {
    devfs_stdout->device = devfs_pick_output_device();
  }
  if (devfs_stderr != NULL && devfs_stdout != NULL) {
    devfs_stderr->device = devfs_stdout->device;
  }
}

static void devfs_on_device_added(device_t* dev) {
  if (dev == NULL) {
    return;
  }
  switch (dev->id) {
    case DEVICE_KEYBOARD:
    case DEVICE_SERIAL:
    case DEVICE_VGA:
    case DEVICE_VGA_QEMU:
    case DEVICE_LCD:
      devfs_bind_stdio();
      break;
    default:
      break;
  }
}

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

  devfs_stdin = stdin;
  devfs_stdout = stdout;
  devfs_stderr = stderr;

  stdin->op = &device_operator;
  stdout->op = &device_operator;
  stderr->op = &device_operator;

  //null
  vnode_t *null = vfs_create_node("null", V_FILE | V_BLOCKDEVICE);
  null->device = device_find(DEVICE_NULL);
  null->op = &device_operator;
  vfs_mount(NULL, "/dev", null);

  device_set_notify(devfs_on_device_added);
  fd_init();

  return 0;
}

void devfs_exit(void) { log_debug("devfs exit\n"); }

module_t devfs_module = {
    .name = "devfs", .init = devfs_init, .exit = devfs_exit};