/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "kernel/kernel.h"
#include "mouse.h"
#include "dev/devfs.h"

int mouse_init(void) {
  device_t* dev = kmalloc(sizeof(device_t),DEFAULT_TYPE);
  dev->name = "mouse";
  // dev->read = read;
  dev->id = DEVICE_MOUSE;
  dev->type = DEVICE_TYPE_CHAR;
  // dev->data = &mouse_device;

  device_add(dev);
  // mouse_device.events = cqueue_create(EVENT_NUMBER, CQUEUE_DROP);



  return 0;
}


void mouse_exit(void) { kprintf("mouse exit\n"); }

module_t mouse_module = {
    .name = "mouse", .init = mouse_init, .exit = mouse_exit};
