/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "foot.h"
#include "service.h"

voperator_t foot_operator = {.write = service_write,
                             .read = service_read,
                             .close = service_close,
                             .open = service_open,
                             .find = service_find,
                             .mount = vfs_mount,
                             .ioctl = service_ioctl,
                             .readdir = service_readdir
};

int foot_init(void) {
  log_debug("foot init\n");

  vnode_t* foot = vfs_create_node("foot", V_FILE);
  vfs_mount(NULL, "/dev", foot);
  foot->op = &foot_operator;

  return 0;
}

void foot_exit(void) { log_debug("foot exit\n"); }

module_t foot_module = {.name = "foot", .init = foot_init, .exit = foot_exit};
