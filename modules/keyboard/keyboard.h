/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "kernel/kernel.h"

/* ===== 手柄专用扩展码段（0x70-0x7E 空闲）========================
 * 手柄字节协议：byte = 键码(低7位) | (松开 ? 0x80 : 0)
 * ⇒ 键码必须 ≤ 0x7F ✗。旧的 KEY_HOME=0xF0 自带 bit7 ⇒ 驱动把它当"松开"入队，
 * 消费端（infones/SDL）&0x7F 后也永远匹配不上 ⇒ HOME 键形同不存在 ✗。
 * 这里为手柄单独开一段扩展码，不再占用键盘码位 ✓。 */
/* 手柄扩展键码段（0x70-0x7E 空闲）：与 KEY_POWER/KEY_BUTTON_* 同族命名 ✓。
 * 键码必须 ≤ 0x7F —— 手柄字节协议是 byte = 键码(低7位) | (松开 ? 0x80 : 0) ✗，
 * 旧的 0xF0 自带 bit7 ⇒ 被当"松开"且消费端 &0x7F 后匹配不上 ✗。 */
#define KEY_HOME        0x70
#define KEY_VOLUMEUP    0x71
#define KEY_VOLUMEDOWN  0x72
/* 兼容旧名（值已修正为 ≤0x7F）*/

#define KEY_ROLL_BACK 0xF1
#define KEY_ROLL_FORWARD 0xF2

#define KEY_RIGHT 4
#define KEY_UP 5
#define CONSOLE_LEFT 8
#define KEY_ENTER 13
#define KEY_LEFT 19
#define KEY_DOWN 24

#define KEY_POWER 26
#define KEY_ESC 27
#define KEY_BACKSPACE 127
#define KEY_BUTTON_A 96
#define KEY_BUTTON_B 97
#define KEY_BUTTON_C 98
#define KEY_BUTTON_L1 102
#define KEY_BUTTON_L2 104
#define KEY_BUTTON_MODE 110
#define KEY_BUTTON_R1 103
#define KEY_BUTTON_R2 105
#define KEY_BUTTON_SELECT 109
#define KEY_BUTTON_START 108
#define KEY_BUTTON_THUMBL 106
#define KEY_BUTTON_THUMBR 107

#define KEY_BUTTON_X 99
#define KEY_BUTTON_Y 100
#define KEY_BUTTON_Z 101

#define JOYSTICK_UP 0x1
#define JOYSTICK_DOWN 0x2
#define JOYSTICK_LEFT 0x4
#define JOYSTICK_RIGHT 0x8
#define JOYSTICK_PRESS 0x10
#define JOYSTICK_BTN_A 0x10
#define JOYSTICK_BTN_B 0x20
#define JOYSTICK_BTN_SELECT 0x40
#define JOYSTICK_BTN_START 0x80

#define MAX_CHARCODE_BUFFER 32

typedef struct keyboard_device {
  u8 scan_code_buffer[MAX_CHARCODE_BUFFER];
  u32 scan_code_index;
  u32 key_release;

} keyboard_device_t;

#endif