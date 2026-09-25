/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * X Window System - Input Device Integration
 ********************************************************************/
#include "xwin.h"
#include "keyboard/keyboard.h"
#include "sysconf/sysconf.h"

// ========== 外部全局显示服务器 ==========
extern xdisplay_t* g_display;

// ========== 键盘状态 ==========
static u8 key_state[256] = {0};

// ========== 扫描码转换表 (PS/2 Scan Code Set 2, US 键盘布局) ==========
// 释放事件已由 PL050 驱动统一转成 make|0x80（见 modules/keyboard/pl050.c），
// 所以这里只维护 make code。运行期可用 /conf/system.conf 的 [input] 段覆盖：
//   [input]
//   keymap = us          # 预设（当前仅 us / set2-us，即下面这张表）
//   key_1d = w           # 逐键覆盖：扫描码(hex) = 字符或 0xNN
static const u32 default_keymap[128] = {
    [0x01] = 0x43,  // F9
    [0x03] = 0x3F,  // F5
    [0x04] = 0x3D,  // F3
    [0x05] = 0x3B,  // F1
    [0x06] = 0x3C,  // F2
    [0x07] = 0x58,  // F12
    [0x09] = 0x44,  // F10
    [0x0A] = 0x42,  // F8
    [0x0B] = 0x40,  // F6
    [0x0C] = 0x3E,  // F4
    [0x0D] = '\t',  // Tab
    [0x0E] = '`',
    [0x11] = 0x12,  // Left Alt
    [0x12] = 0x10,  // Left Shift
    [0x14] = 0x11,  // Left Ctrl
    [0x15] = 'q',   [0x16] = '1',   [0x1A] = 'z',   [0x1B] = 's',
    [0x1C] = 'a',   [0x1D] = 'w',   [0x1E] = '2',
    [0x21] = 'c',   [0x22] = 'x',   [0x23] = 'd',   [0x24] = 'e',
    [0x25] = '4',   [0x26] = '3',
    [0x29] = ' ',   // Space
    [0x2A] = 'v',   [0x2B] = 'f',   [0x2C] = 't',   [0x2D] = 'r',
    [0x2E] = '5',
    [0x31] = 'n',   [0x32] = 'b',   [0x33] = 'h',   [0x34] = 'g',
    [0x35] = 'y',   [0x36] = '6',
    [0x3A] = 'm',   [0x3B] = 'j',   [0x3C] = 'u',   [0x3D] = '7',
    [0x3E] = '8',
    [0x41] = ',',   [0x42] = 'k',   [0x43] = 'i',   [0x44] = 'o',
    [0x45] = '0',   [0x46] = '9',
    [0x49] = '.',   [0x4A] = '/',   [0x4B] = 'l',   [0x4C] = ';',
    [0x4D] = 'p',   [0x4E] = '-',
    [0x52] = '\'',
    [0x54] = '[',   [0x55] = '=',
    [0x58] = 0x14,  // Caps Lock
    [0x59] = 0x10,  // Right Shift
    [0x5A] = '\n',  // Enter
    [0x5B] = ']',   [0x5D] = '\\',
    [0x66] = '\b',  // Backspace
    [0x69] = '1',   // Keypad 1
    [0x6B] = '4',   // Keypad 4
    [0x6C] = '7',   // Keypad 7
    [0x70] = '0',   // Keypad 0
    [0x71] = '.',   // Keypad .
    [0x72] = '2',   // Keypad 2
    [0x73] = '5',   // Keypad 5
    [0x74] = '6',   // Keypad 6
    [0x75] = '8',   // Keypad 8
    [0x76] = 0x1B,  // ESC
    [0x78] = 0x57,  // F11
    [0x79] = '+',   // Keypad +
    [0x7A] = '3',   // Keypad 3
    [0x7B] = '-',   // Keypad -
    [0x7C] = '*',   // Keypad *
    [0x7D] = '9',   // Keypad 9
};

/* Scan Code Set 1 (XT)。驱动上报的是"原始扫描码 + release|0x80"，
 * 选哪张表由 /conf/system.conf 的 [input] scanset 决定（默认 2）。 */
static const u32 default_keymap_set1[128] = {
    [0x01] = 0x1B,  // ESC
    [0x02] = '1',   [0x03] = '2',   [0x04] = '3',   [0x05] = '4',
    [0x06] = '5',   [0x07] = '6',   [0x08] = '7',   [0x09] = '8',
    [0x0A] = '9',   [0x0B] = '0',   [0x0C] = '-',   [0x0D] = '=',
    [0x0E] = '\b',  [0x0F] = '\t',
    [0x10] = 'q',   [0x11] = 'w',   [0x12] = 'e',   [0x13] = 'r',
    [0x14] = 't',   [0x15] = 'y',   [0x16] = 'u',   [0x17] = 'i',
    [0x18] = 'o',   [0x19] = 'p',   [0x1A] = '[',   [0x1B] = ']',
    [0x1C] = '\n',  [0x1D] = 0x11,  [0x1E] = 'a',   [0x1F] = 's',
    [0x20] = 'd',   [0x21] = 'f',   [0x22] = 'g',   [0x23] = 'h',
    [0x24] = 'j',   [0x25] = 'k',   [0x26] = 'l',   [0x27] = ';',
    [0x28] = '\'',  [0x29] = '`',   [0x2A] = 0x10,  [0x2B] = '\\',
    [0x2C] = 'z',   [0x2D] = 'x',   [0x2E] = 'c',   [0x2F] = 'v',
    [0x30] = 'b',   [0x31] = 'n',   [0x32] = 'm',   [0x33] = ',',
    [0x34] = '.',   [0x35] = '/',   [0x36] = 0x10,  [0x38] = 0x12,
    [0x39] = ' ',   [0x3A] = 0x14,
    [0x3B] = 0x3B,  [0x3C] = 0x3C,  [0x3D] = 0x3D,  [0x3E] = 0x3E,
    [0x3F] = 0x3F,  [0x40] = 0x40,  [0x41] = 0x41,  [0x42] = 0x42,
    [0x43] = 0x43,  [0x44] = 0x44,
    [0x48] = 0x47,  [0x49] = 0x49,  [0x4A] = '/',   [0x4B] = 0x4B,
    [0x4C] = 0x4C,  [0x4D] = 0x4D,  [0x4F] = 0x4F,  [0x50] = 0x50,
    [0x51] = 0x51,  [0x52] = 0x52,  [0x53] = 0x53,  [0x57] = 0x57,
    [0x58] = 0x58,
};

/* 运行期生效的键映射（启动时由 xinput_load_keymap 用所选预设初始化，
 * 再用 /conf/system.conf 的 [input] 段覆盖）。 */
static u32 g_keymap[128];

/* E0 扩展键（方向键/Home/End/Ins/Del/右 Ctrl 等）。它们的 make code 与普通键
 * 有重叠（如 0x74 普通=KP6、E0 0x74=Right），所以必须单独一张表。 */
static const u32 default_keymap_ext[128] = {
    [0x6B] = 0x4B,  // Left
    [0x74] = 0x4D,  // Right
    [0x75] = 0x48,  // Up
    [0x72] = 0x50,  // Down
    [0x6C] = 0x47,  // Home
    [0x69] = 0x4F,  // End
    [0x7D] = 0x49,  // Page Up
    [0x7A] = 0x51,  // Page Down
    [0x70] = 0x52,  // Insert
    [0x71] = 0x53,  // Delete
    [0x14] = 0x11,  // Right Ctrl
    [0x11] = 0x12,  // Right Alt
    [0x5A] = '\n',  // Keypad Enter
    [0x4A] = '/',   // Keypad /
};

static u8 g_kbd_e0; /* 上一个字节是 0xE0 扩展前缀 */

// ========== 处理键盘扫描码 ==========
void xinput_keyboard_event(u8 scancode) {
    if (g_display == NULL) return;
    
    u32 pressed = 1;
    u8 code = scancode;
    
    // 0xE0 扩展前缀：记住，下一字节查扩展表
    if (scancode == 0xE0) {
        g_kbd_e0 = 1;
        return;
    }
    
    if (scancode & 0x80) {
        pressed = 0;
        code = scancode & 0x7F;
    }
    
    // 获取键码（扩展键走 E0 表；0xF0 前缀已由 pl050 驱动消化）
    u32 keycode = g_kbd_e0 ? default_keymap_ext[code] : g_keymap[code];
    g_kbd_e0 = 0;
    if (keycode == 0 && code != 0) {
        keycode = code;  // 未映射的键，使用扫描码
    }
    
    // 更新键状态
    if (code < 128) {
        key_state[code] = pressed;
    }
    
    // 计算修饰键状态（Set 2：Shift=0x12/0x59, Ctrl=0x14, Alt=0x11）
    u32 mods = 0;
    if (key_state[0x12] || key_state[0x59]) mods |= 0x01;  // Shift
    if (key_state[0x14]) mods |= 0x02;  // Ctrl
    if (key_state[0x11]) mods |= 0x04;  // Alt
    
    // 发送键盘事件
    xwin_keyboard_event(g_display, keycode, pressed, mods);
}

// ========== 手柄（joystick）按键 ==========
// GPIO 手柄驱动（modules/keyboard/{ssd202d,t113-s3,v3s,...}.c）注册为
// DEVICE_JOYSTICK，上报 Linux KEY_* 码流（release = code|0x80），与 keyboard
// 的 PS/2 set2 是两条独立通道。这里用手柄专用默认映射表把 KEY_* 转成 xwin 的
// keycode（方向键/Return/Esc/x/y...，与应用层 yui backend_sdl.c 一致），
// 不经过 default_keymap（那是给 set2 扫描码用的）。
static const u32 default_joymap[256] = {
    [KEY_UP] = 0x48,
    [KEY_DOWN] = 0x50,
    [KEY_LEFT] = 0x4B,
    [KEY_RIGHT] = 0x4D,
    [KEY_BUTTON_A] = 0x0A,       // Return
    [KEY_BUTTON_B] = 0x1B,       // Esc
    [KEY_BUTTON_X] = 'x',
    [KEY_BUTTON_Y] = 'y',
    [KEY_BUTTON_START] = 0x0A,   // Return
    [KEY_BUTTON_SELECT] = 0x1B,  // Esc
    [KEY_BUTTON_L1] = 0x3B,      // F1
    [KEY_BUTTON_R1] = 0x3C,      // F2
    [KEY_BUTTON_L2] = 0x3D,      // F3
    [KEY_BUTTON_R2] = 0x3E,      // F4
    [KEY_HOME] = 0x47,           // Home
    [KEY_POWER] = 0x1B,          // Esc
};

void xinput_joystick_event(u8 code) {
    if (g_display == NULL) return;
    u32 pressed = 1;
    if (code & 0x80) {
        pressed = 0;
        code &= 0x7F;
    }
    u32 keycode = default_joymap[code];
    if (keycode == 0) return; /* 未映射的键忽略 */
    xwin_keyboard_event(g_display, keycode, pressed, g_display->key_mods);
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
    
    // PS/2 鼠标 Y 正方向为"向上"，而屏幕坐标向下为正，需反转
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
    
    // 轮询手柄设备（GPIO 手柄，上报 KEY_* 码流）
    device_t* joy = device_find(DEVICE_JOYSTICK);
    if (joy != NULL && joy->read != NULL) {
        u8 code;
        while (joy->read(joy, &code, 1) > 0) {
            xinput_joystick_event(code);
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

// ========== 键盘映射配置（/conf/system.conf 的 [input] 段） ==========
static u32 xinput_hex(const char* s) {
    u32 v = 0;
    while (s != NULL && *s != 0) {
        char c = *s++;
        u32 d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        v = v * 16 + d;
    }
    return v;
}

static u32 xinput_parse_keycode(const char* s) {
    if (s == NULL || s[0] == 0) return 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) return xinput_hex(s + 2);
    if (kstrncmp(s, "space", 5) == 0) return ' ';
    if (kstrncmp(s, "enter", 5) == 0) return '\n';
    if (kstrncmp(s, "tab", 3) == 0) return '\t';
    if (kstrncmp(s, "esc", 3) == 0) return 0x1B;
    if (kstrncmp(s, "backspace", 9) == 0) return '\b';
    if (kstrncmp(s, "shift", 5) == 0) return 0x10;
    if (kstrncmp(s, "ctrl", 4) == 0) return 0x11;
    if (kstrncmp(s, "alt", 3) == 0) return 0x12;
    if (s[0] >= '0' && s[0] <= '9') return xinput_hex(s); /* 十进制数值 */
    return (u32)(u8)s[0];
}

static int xinput_keymap_cb(const char* key, const char* val, void* user) {
    (void)user;
    if (kstrncmp(key, "key_", 4) == 0) {
        u32 scan = xinput_hex(key + 4);
        if (scan < 128) g_keymap[scan] = xinput_parse_keycode(val);
    }
    /* keymap = us / set2-us：内置表即该预设，暂无需处理 */
    return 1;
}

void xinput_load_keymap(void) {
    int set = 2;
    if (sysconf_loaded()) {
        set = sysconf_get_int("input", "scanset", 2);
    }
    if (set == 1) {
        kmemcpy(g_keymap, default_keymap_set1, sizeof(g_keymap));
    } else {
        set = 2;
        kmemcpy(g_keymap, default_keymap, sizeof(g_keymap));
    }
    if (sysconf_loaded()) {
        int n = sysconf_foreach("input", xinput_keymap_cb, NULL);
        log_info("xinput: keymap set%d default+%d custom\n", set, n);
    } else {
        log_info("xinput: keymap set%d default (no system.conf)\n", set);
    }
}

// ========== 初始化输入子系统 ==========
void xinput_init(void) {
    xinput_load_keymap();
    log_info("xinput: initialized\n");
}

// ========== 清理输入子系统 ==========
void xinput_exit(void) {
    log_info("xinput: exited\n");
}
