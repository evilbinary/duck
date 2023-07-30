/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "gaga.h"
#include "service.h"

voperator_t gaga_operator = {.write = service_write,
                             .read = service_read,
                             .close = service_close,
                             .open = service_open,
                             .find = service_find,
                             .mount = vfs_mount,
                             .ioctl = service_ioctl,
                             .readdir = vfs_readdir};

int gaga_init(void) {
  log_debug("gaga init\n");

  vnode_t* gaga = vfs_create_node("gaga", V_FILE);
  vfs_mount(NULL, "/dev", gaga);
  gaga->op = &gaga_operator;

  return 0;
}

void gaga_exit(void) { log_debug("gaga exit\n"); }

module_t gaga_module = {.name = "gaga", .init = gaga_init, .exit = gaga_exit};
