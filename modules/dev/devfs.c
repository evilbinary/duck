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
  device_t* dev = device_find(DEVICE_SERIAL);
  if (dev != NULL) {
    return dev;
  }
  dev = device_find(DEVICE_VGA);
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
  return NULL;
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
      kprintf("bind stdio %d\n", dev->id);
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

/* ---------------- 设备节点绑定（实现见 devfs.h 的说明） ---------------- */

device_t* devfs_display_primary(void) {
  static const u32 prio[] = {DEVICE_VGA, DEVICE_VGA_QEMU, DEVICE_LCD};

  for (u32 i = 0; i < sizeof(prio) / sizeof(prio[0]); i++) {
    device_t* dev = device_find(prio[i]);
    if (dev != NULL) {
      return dev;
    }
  }
  return NULL;
}

static const char* devfs_dev_name(device_t* dev) {
  return (dev != NULL && dev->name != NULL) ? dev->name : "(none)";
}

int devfs_bind_device(const char* path, device_t* dev) {
  vnode_t* node;
  const char* base;

  /* 只允许 /dev/<name>：配置来自外部文件，不能让它可以绑到 /etc、/bin 上 */
  if (path == NULL || kstrncmp(path, "/dev/", 5) != 0 || path[5] == 0) {
    log_warn("devfs: bind: only /dev/<name> allowed, got %s\n",
             path != NULL ? path : "(null)");
    return -1;
  }
  base = path + 5;

  node = vfs_find(NULL, (u8*)path);
  if (node == NULL) {
    /* 节点不存在就建：这样"某平台根本没有 /dev/fb"也能靠配置造出来
     * （例如只有 SPI 屏的平台：fb = lcd） */
    node = vfs_create_node((u8*)base, V_FILE);
    if (node == NULL) {
      log_error("devfs: bind: create %s failed\n", path);
      return -1;
    }
    vfs_mount(NULL, (u8*)"/dev", node);
  } else if (node->device != NULL && (device_t*)node->device != dev) {
    /* 换绑：留一条记录，"谁把谁顶掉了"一眼可见 */
    log_warn("devfs: %s rebind %s -> %s\n", path,
             devfs_dev_name((device_t*)node->device), devfs_dev_name(dev));
  }

  node->device = dev;
  node->op = dev != NULL ? &device_operator : &no_rw_operator;
  log_info("devfs: %s -> %s\n", path, devfs_dev_name(dev));
  return 0;
}

int devfs_bind_path(const char* path, const char* name) {
  device_t* dev;

  if (name == NULL || name[0] == 0) {
    log_warn("devfs: bind: empty device name for %s, skipped\n",
             path != NULL ? path : "(null)");
    return -1;
  }
  if (kstrcmp(name, "none") == 0 || kstrcmp(name, "-") == 0) {
    return devfs_bind_device(path, NULL); /* 显式解绑 */
  }
  dev = device_find_name((char*)name);
  if (dev == NULL) {
    log_warn("devfs: bind: no device named \"%s\" for %s\n", name, path);
    return -1;
  }
  return devfs_bind_device(path, dev);
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
  devfs_bind_stdio();
  fd_init();

  return 0;
}

void devfs_exit(void) { log_debug("devfs exit\n"); }

module_t devfs_module = {
    .name = "devfs", .init = devfs_init, .exit = devfs_exit};