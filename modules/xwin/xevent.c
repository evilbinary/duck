/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * X Window System - Event Handling
 ********************************************************************/
#include "xwin.h"

// ========== 事件处理 ==========

void xwin_process_events(xdisplay_t* disp) {
    if (disp == NULL) return;
    
    xevent_t event;
    while (xwin_next_event(disp, &event)) {
        xwin_dispatch_event(disp, &event);
    }
}

int xwin_next_event(xdisplay_t* disp, xevent_t* event) {
    if (disp == NULL || disp->event_queue == NULL || event == NULL) {
        return 0;
    }
    
    return ring_queue_poll(disp->event_queue, event);
}

void xwin_send_event(xdisplay_t* disp, xwindow_t* win, xevent_t* event) {
    if (win == NULL || event == NULL) return;
    
    // 设置窗口ID
    event->window_id = win->id;
    
    // 如果窗口有事件回调，直接调用
    if (win->on_event != NULL) {
        win->on_event(win, event);
    }
    
    // 同时加入事件队列
    xwin_post_event(disp, event);
}

void xwin_post_event(xdisplay_t* disp, xevent_t* event) {
    if (disp == NULL || disp->event_queue == NULL || event == NULL) {
        return;
    }
    
    ring_queue_put(disp->event_queue, event);
}

void xwin_dispatch_event(xdisplay_t* disp, xevent_t* event) {
    if (disp == NULL || event == NULL) return;

    xwindow_t* win = xwin_find_window(disp, event->window_id);
    if (win == NULL) return;

    // 默认事件处理
    switch (event->type) {
        case XEVENT_MOUSE_DOWN:
            // 设置焦点
            if (win->flags & XWIN_FLAG_FOCUSABLE) {
                xwin_set_focus(disp, win);
            }
            break;

        case XEVENT_MOUSE_UP:
            // 结束拖拽
            if (disp->drag_window != NULL) {
                disp->drag_window = NULL;
            }
            break;

        case XEVENT_MOUSE_MOVE:
            // 拖拽在 xwin_mouse_move 中直接处理，这里只处理其他移动逻辑
            break;

        case XEVENT_EXPOSE:
            // 标记窗口需要重绘
            xwin_damage_all(win);
            break;

        case XEVENT_RESIZE:
            // 窗口大小改变，重绘
            xwin_damage_all(win);
            break;

        default:
            break;
    }
}

// ========== 输入事件 ==========

void xwin_mouse_move(xdisplay_t* disp, i32 x, i32 y) {
    if (disp == NULL) return;

    i32 rel_x = x - disp->mouse_x;
    i32 rel_y = y - disp->mouse_y;

    disp->mouse_x = x;
    disp->mouse_y = y;

    // 如果正在拖拽，直接处理窗口移动
    if (disp->drag_window != NULL) {
        xwindow_t* drag_win = disp->drag_window;
        i32 new_x = x - disp->drag_offset_x;
        i32 new_y = y - disp->drag_offset_y;
        xwin_move(disp, drag_win, new_x, new_y);
        return;  // 拖拽时不发送其他事件
    }

    // 查找鼠标下的窗口
    xwindow_t* win = xwin_find_window_at(disp, x, y);
    if (win == NULL) win = disp->root_window;

    // 发送鼠标移动事件
    xevent_t event;
    kmemset(&event, 0, sizeof(event));
    event.type = XEVENT_MOUSE_MOVE;
    event.window_id = win->id;
    event.data.mouse.x = x - win->abs_x;
    event.data.mouse.y = y - win->abs_y;
    event.data.mouse.rel_x = rel_x;
    event.data.mouse.rel_y = rel_y;
    event.data.mouse.button = disp->mouse_buttons;

    xwin_send_event(disp, win, &event);
}

// 外部函数声明
extern int xwm_handle_mouse_down(xdisplay_t* disp, xwindow_t* win, i32 x, i32 y, u32 button);
extern int xwm_handle_mouse_up(xdisplay_t* disp, xwindow_t* win, i32 x, i32 y, u32 button);

void xwin_mouse_button(xdisplay_t* disp, u32 button, u32 pressed) {
    if (disp == NULL) return;

    // 更新按键状态
    if (pressed) {
        disp->mouse_buttons |= button;
    } else {
        disp->mouse_buttons &= ~button;
    }

    // 查找鼠标下的窗口
    xwindow_t* win = xwin_find_window_at(disp, disp->mouse_x, disp->mouse_y);
    if (win == NULL) win = disp->root_window;

    i32 x = disp->mouse_x - win->abs_x;
    i32 y = disp->mouse_y - win->abs_y;

    // 窗口管理器先处理（标题栏拖拽、按钮等）
    int handled = 0;
    if (pressed) {
        handled = xwm_handle_mouse_down(disp, win, x, y, button);
    } else {
        handled = xwm_handle_mouse_up(disp, win, x, y, button);
    }

    // 如果窗口管理器已处理，不再传递给应用
    if (handled) return;

    // 发送按键事件给应用
    xevent_t event;
    kmemset(&event, 0, sizeof(event));
    event.type = pressed ? XEVENT_MOUSE_DOWN : XEVENT_MOUSE_UP;
    event.window_id = win->id;
    event.data.mouse.x = x;
    event.data.mouse.y = y;
    event.data.mouse.button = button;

    // 调用应用回调
    if (win->on_event != NULL) {
        win->on_event(win, &event);
    }

    // 加入事件队列
    xwin_post_event(disp, &event);
}

void xwin_mouse_wheel(xdisplay_t* disp, i32 delta) {
    if (disp == NULL) return;
    
    // 查找鼠标下的窗口
    xwindow_t* win = xwin_find_window_at(disp, disp->mouse_x, disp->mouse_y);
    if (win == NULL) win = disp->root_window;
    
    // 发送滚轮事件
    xevent_t event;
    kmemset(&event, 0, sizeof(event));
    event.type = XEVENT_MOUSE_WHEEL;
    event.window_id = win->id;
    event.data.mouse.x = disp->mouse_x - win->abs_x;
    event.data.mouse.y = disp->mouse_y - win->abs_y;
    event.data.mouse.wheel = delta;
    
    xwin_send_event(disp, win, &event);
}

void xwin_keyboard_event(xdisplay_t* disp, u32 keycode, u32 pressed, u32 mods) {
    if (disp == NULL) return;
    
    // 更新修饰键状态
    disp->key_mods = mods;
    
    // 发送到焦点窗口
    xwindow_t* win = disp->focused_window;
    if (win == NULL) win = disp->root_window;
    
    xevent_t event;
    kmemset(&event, 0, sizeof(event));
    event.type = pressed ? XEVENT_KEY_DOWN : XEVENT_KEY_UP;
    event.window_id = win->id;
    event.data.key.keycode = keycode;
    event.data.key.pressed = pressed;
    event.data.key.mods = mods;
    
    // 转换为字符 (简化版)
    if (keycode < 128 && pressed) {
        event.data.key.ch = (char)keycode;
        // 处理Shift
        if (mods & 0x01) {
            if (event.data.key.ch >= 'a' && event.data.key.ch <= 'z') {
                event.data.key.ch -= 32;
            }
        }
    }
    
    xwin_send_event(disp, win, &event);
}
