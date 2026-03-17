/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * X Window System - Input Device Integration
 ********************************************************************/
#include "xwin.h"

// ========== 外部全局显示服务器 ==========
extern xdisplay_t* g_display;

// ========== 键盘状态 ==========
static u8 key_state[256] = {0};

// ========== 扫描码转换表 (US 键盘布局) ==========
static const u32 scancode_to_keycode[128] = {
    [0x01] = 0x1B,  // ESC
    [0x02] = '1',   [0x03] = '2',   [0x04] = '3',
    [0x05] = '4',   [0x06] = '5',   [0x07] = '6',
    [0x08] = '7',   [0x09] = '8',   [0x0A] = '9',
    [0x0B] = '0',   [0x0C] = '-',   [0x0D] = '=',
    [0x0E] = '\b',  // Backspace
    [0x0F] = '\t',  // Tab
    [0x10] = 'q',   [0x11] = 'w',   [0x12] = 'e',   [0x13] = 'r',
    [0x14] = 't',   [0x15] = 'y',   [0x16] = 'u',   [0x17] = 'i',
    [0x18] = 'o',   [0x19] = 'p',   [0x1A] = '[',   [0x1B] = ']',
    [0x1C] = '\n',  // Enter
    [0x1D] = 0x11,  // Left Ctrl
    [0x1E] = 'a',   [0x1F] = 's',   [0x20] = 'd',   [0x21] = 'f',
    [0x22] = 'g',   [0x23] = 'h',   [0x24] = 'j',   [0x25] = 'k',
    [0x26] = 'l',   [0x27] = ';',   [0x28] = '\'',  [0x29] = '`',
    [0x2A] = 0x10,  // Left Shift
    [0x2B] = '\\',  [0x2C] = 'z',   [0x2D] = 'x',   [0x2E] = 'c',
    [0x2F] = 'v',   [0x30] = 'b',   [0x31] = 'n',   [0x32] = 'm',
    [0x33] = ',',   [0x34] = '.',   [0x35] = '/',
    [0x36] = 0x10,  // Right Shift
    [0x38] = 0x12,  // Left Alt
    [0x39] = ' ',   // Space
    [0x3A] = 0x14,  // Caps Lock
    [0x3B] = 0x3B,  // F1
    [0x3C] = 0x3C,  // F2
    [0x3D] = 0x3D,  // F3
    [0x3E] = 0x3E,  // F4
    [0x3F] = 0x3F,  // F5
    [0x40] = 0x40,  // F6
    [0x41] = 0x41,  // F7
    [0x42] = 0x42,  // F8
    [0x43] = 0x43,  // F9
    [0x44] = 0x44,  // F10
    [0x48] = 0x47,  // Insert
    [0x49] = 0x49,  // Page Up
    [0x4A] = '/',   // Keypad /
    [0x4B] = 0x4B,  // Left Arrow
    [0x4C] = 0x4C,  // Keypad 5
    [0x4D] = 0x4D,  // Right Arrow
    [0x4F] = 0x4F,  // End
    [0x50] = 0x50,  // Down Arrow
    [0x51] = 0x51,  // Page Down
    [0x52] = 0x52,  // Insert
    [0x53] = 0x53,  // Delete
    [0x57] = 0x57,  // F11
    [0x58] = 0x58,  // F12
};

// ========== 处理键盘扫描码 ==========
void xinput_keyboard_event(u8 scancode) {
    if (g_display == NULL) return;
    
    u32 pressed = 1;
    u8 code = scancode;
    
    // 处理释放码 (0xE0 前缀 或 0x80 释放标志)
    if (scancode == 0xE0) {
        // 扩展键，需要下一个字节
        return;
    }
    
    if (scancode & 0x80) {
        pressed = 0;
        code = scancode & 0x7F;
    }
    
    // 获取键码
    u32 keycode = scancode_to_keycode[code];
    if (keycode == 0 && code != 0) {
        keycode = code;  // 未映射的键，使用扫描码
    }
    
    // 更新键状态
    if (code < 128) {
        key_state[code] = pressed;
    }
    
    // 计算修饰键状态
    u32 mods = 0;
    if (key_state[0x2A] || key_state[0x36]) mods |= 0x01;  // Shift
    if (key_state[0x1D]) mods |= 0x02;  // Ctrl
    if (key_state[0x38]) mods |= 0x04;  // Alt
    
    // 发送键盘事件
    xwin_keyboard_event(g_display, keycode, pressed, mods);
}

// ========== 处理鼠标移动 ==========
void xinput_mouse_move(i32 dx, i32 dy) {
    if (g_display == NULL) return;
    
    i32 new_x = g_display->mouse_x + dx;
    i32 new_y = g_display->mouse_y + dy;
    
    // 限制在屏幕范围内
    if (new_x < 0) new_x = 0;
    if (new_y < 0) new_y = 0;
    if (g_display->vga != NULL) {
        if (new_x >= (i32)g_display->vga->width) 
            new_x = g_display->vga->width - 1;
        if (new_y >= (i32)g_display->vga->height) 
            new_y = g_display->vga->height - 1;
    }
    
    xwin_mouse_move(g_display, new_x, new_y);
}

// ========== 处理鼠标按键 ==========
void xinput_mouse_button(u32 button, u32 pressed) {
    if (g_display == NULL) return;
    xwin_mouse_button(g_display, button, pressed);
}

// ========== 处理鼠标滚轮 ==========
void xinput_mouse_wheel(i32 delta) {
    if (g_display == NULL) return;
    xwin_mouse_wheel(g_display, delta);
}

// ========== PS/2 鼠标数据包解析 ==========
void xinput_ps2_mouse_data(u8* data) {
    // PS/2 鼠标数据包格式:
    // Byte 0: Y溢出, X溢出, Y符号, X符号, 1, 中键, 右键, 左键
    // Byte 1: X移动
    // Byte 2: Y移动
    // Byte 3: 滚轮 (仅 IntelliMouse)
    
    u8 buttons = data[0] & 0x07;
    
    // 计算移动量
    i32 dx = data[1];
    i32 dy = data[2];
    
    // 符号扩展
    if (data[0] & 0x10) dx -= 256;
    if (data[0] & 0x20) dy -= 256;
    
    // Y 轴反向
    dy = -dy;
    
    // 鼠标移动
    xinput_mouse_move(dx, dy);
    
    // 按键状态
    static u8 old_buttons = 0;
    
    if ((buttons & 0x01) != (old_buttons & 0x01)) {
        xinput_mouse_button(XBUTTON_LEFT, buttons & 0x01);
    }
    if ((buttons & 0x02) != (old_buttons & 0x02)) {
        xinput_mouse_button(XBUTTON_RIGHT, (buttons >> 1) & 0x01);
    }
    if ((buttons & 0x04) != (old_buttons & 0x04)) {
        xinput_mouse_button(XBUTTON_MIDDLE, (buttons >> 2) & 0x01);
    }
    
    old_buttons = buttons;
    
    // 滚轮 (如果有第4字节)
    // xinput_mouse_wheel(data[3]);
}

// ========== 输入轮询 ==========
void xinput_poll(void) {
    if (g_display == NULL) return;
    
    // 轮询键盘设备
    device_t* kbd = device_find(DEVICE_KEYBOARD);
    if (kbd != NULL && kbd->read != NULL) {
        u8 scancode;
        while (kbd->read(kbd, &scancode, 1) > 0) {
            xinput_keyboard_event(scancode);
        }
    }
    
    // 轮询鼠标设备
    device_t* mouse_dev = device_find(DEVICE_MOUSE);
    // log_debug("xinput_poll: mouse_dev=%p DEVICE_MOUSE=%d\n", mouse_dev, DEVICE_MOUSE);
    if (mouse_dev != NULL && mouse_dev->read != NULL) {
        u8 data[4];
        while (mouse_dev->read(mouse_dev, data, 3) >= 3) {
            xinput_ps2_mouse_data(data);
        }
    }
}

// ========== 初始化输入子系统 ==========
void xinput_init(void) {
    log_info("xinput: initialized\n");
}

// ========== 清理输入子系统 ==========
void xinput_exit(void) {
    log_info("xinput: exited\n");
}
