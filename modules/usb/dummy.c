/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
/* 平台没有 USB host 实现时的兜底（例如 miyoo）。
 *
 * 【为什么需要】`duck/modules/usb/ya.py` 的 common_source 里包含 usb.c，而
 * usb.c 会**无条件**调用 usb_host_init()/usb_device_init()；平台若没在
 * plat_source 里列出专用实现（raspi2/raspi3 用 dwc2.c、v3s/t113-s3 用 sunxi.c），
 * 链接期就会 `undefined reference to 'usb_host_init'`。
 * 本文件提供一对空实现，保证这类平台也能链接通过（USB 不可用，但内核能起）。
 */
#include "usb.h"

void usb_host_init(void) {
  /* no USB host controller on this platform */
}

void usb_device_init(void) {
  /* no USB device mode on this platform */
}
