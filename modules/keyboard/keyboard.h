/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "kernel/kernel.h"

#define KEY_HOME 0xF0
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