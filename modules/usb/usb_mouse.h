/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef USB_MOUSE_H
#define USB_MOUSE_H

#include "kernel/kernel.h"

/* 取一份 PS/2 兼容鼠标包（相对位移）：
 *   byte0: Y溢出, X溢出, Y符号, X符号, 1, 中键, 右键, 左键
 *   byte1: X 位移（补码低 8 位）
 *   byte2: Y 位移（补码低 8 位）
 *   byte3: 滚轮（len >= 4 时才有）
 * 无数据返回 0；有数据返回实际写入的字节数（3 或 4）。
 * 该调用会同步消费一次中断端点：HID 中断端点上报的是"设备端最新一份
 * 报告"，主机不读就被下一份覆盖，因此没有队列溢出问题。 */
size_t usb_mouse_read_packet(u8* buf, size_t len);

/* 主动轮询一次所有 USB 鼠标的中断端点（一般由读接口内部驱动）。 */
void usb_mouse_poll(void);

#endif
