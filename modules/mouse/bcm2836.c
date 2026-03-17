/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "kernel/kernel.h"
#include "mouse.h"
#include "dev/devfs.h"

int mouse_init(void) {
  // BCM2836/BCM2837 没有 PS/2 鼠标，USB 鼠标由 usb_mouse 驱动处理
  // 不注册 DEVICE_MOUSE，避免占用设备 ID
  return 0;
}


void mouse_exit(void) { kprintf("mouse exit\n"); }

module_t mouse_module = {
    .name = "mouse", .init = mouse_init, .exit = mouse_exit};