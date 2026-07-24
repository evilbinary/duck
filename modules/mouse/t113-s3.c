/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "kernel/kernel.h"
#include "mouse.h"
#include "dev/devfs.h"

/* T113 暂无真实鼠标驱动时，注册空字符设备并挂到 /dev/mouse。 */

static mouse_device_t mouse_device;

static size_t mouse_read(device_t* dev, void* buf, size_t len) {
  mouse_event_t* data;
  (void)dev;
  if (buf == NULL || len < sizeof(mouse_event_t)) {
    return 0;
  }
  data = (mouse_event_t*)buf;
  data->sate = 0;
  data->x = (i32)mouse_device.x;
  data->y = (i32)mouse_device.y;
  return sizeof(mouse_event_t);
}

int mouse_init(void) {
  device_t* dev = kmalloc(sizeof(device_t), KERNEL_TYPE);
  if (dev == NULL) {
    return -1;
  }
  kmemset(dev, 0, sizeof(device_t));
  kmemset(&mouse_device, 0, sizeof(mouse_device));

  dev->name = "mouse";
  dev->read = mouse_read;
  dev->id = DEVICE_MOUSE;
  dev->type = DEVICE_TYPE_CHAR;
  dev->data = &mouse_device;
  device_add(dev);

  {
    device_t* mouse_dev = device_find(DEVICE_MOUSE);
    if (mouse_dev != NULL) {
      vnode_t* mouse = vfs_create_node("mouse", V_FILE);
      vfs_mount(NULL, "/dev", mouse);
      mouse->device = mouse_dev;
      mouse->op = &device_operator;
    } else {
      kprintf("dev mouse not found\n");
    }
  }
  return 0;
}

void mouse_exit(void) { kprintf("mouse exit\n"); }

module_t mouse_module = {
    .name = "mouse", .init = mouse_init, .exit = mouse_exit};
