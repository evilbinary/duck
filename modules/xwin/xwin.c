/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * X Window System - Core Implementation
 ********************************************************************/
#include "xwin.h"
#include "kernel/memory.h"

#define MAX_WINDOWS 64

// ========== 全局显示服务器 ==========
xdisplay_t* g_display = NULL;

// ========== 初始化 ==========

int xwin_init(xdisplay_t* disp, vga_device_t* vga) {
    if (disp == NULL || vga == NULL) {
        return -1;
    }

    // 设置全局显示服务器
    g_display = disp;

    kmemset(disp, 0, sizeof(xdisplay_t));
    disp->vga = vga;
    
    // 计算缓冲区大小
    disp->buffer_size = vga->width * vga->height * sizeof(u32);
    
    // 分配屏幕缓冲区
    disp->screen_buffer = kmalloc(disp->buffer_size, KERNEL_TYPE);
    if (disp->screen_buffer == NULL) {
        log_error("xwin: failed to allocate screen buffer\n");
        return -1;
    }
    kmemset(disp->screen_buffer, 0, disp->buffer_size);
    
    // 分配后备缓冲区 (双缓冲)
    disp->back_buffer = kmalloc(disp->buffer_size, KERNEL_TYPE);
    if (disp->back_buffer == NULL) {
        log_error("xwin: failed to allocate back buffer\n");
        kfree(disp->screen_buffer);
        return -1;
    }
    kmemset(disp->back_buffer, 0, disp->buffer_size);
    
    // 分配窗口数组
    disp->window_capacity = MAX_WINDOWS;
    disp->windows = kmalloc(sizeof(xwindow_t*) * disp->window_capacity, KERNEL_TYPE);
    if (disp->windows == NULL) {
        log_error("xwin: failed to allocate window array\n");
        kfree(disp->screen_buffer);
        kfree(disp->back_buffer);
        return -1;
    }
    kmemset(disp->windows, 0, sizeof(xwindow_t*) * disp->window_capacity);
    
    // 创建事件队列
    disp->event_queue = queue_pool_create(256, sizeof(xevent_t));
    if (disp->event_queue == NULL) {
        log_error("xwin: failed to create event queue\n");
        kfree(disp->screen_buffer);
        kfree(disp->back_buffer);
        kfree(disp->windows);
        return -1;
    }
    
    // 创建根窗口 (桌面)
    disp->root_window = xwin_create_window(disp, NULL, 0, 0, 
                                            vga->width, vga->height,
                                            XWIN_FLAG_ROOT | XWIN_FLAG_VISIBLE);
    if (disp->root_window == NULL) {
        log_error("xwin: failed to create root window\n");
        queue_pool_destroy(disp->event_queue);
        kfree(disp->screen_buffer);
        kfree(disp->back_buffer);
        kfree(disp->windows);
        return -1;
    }
    disp->root_window->bg_color = XCOLOR_DARK_GRAY;
    xwin_set_title(disp->root_window, "Desktop");
    
    // 初始化鼠标状态
    disp->mouse_x = vga->width / 2;
    disp->mouse_y = vga->height / 2;
    disp->mouse_visible = 1;
    disp->mouse_cursor = 0;
    
    // 初始化窗口ID计数器
    disp->next_window_id = 1;
    
    g_display = disp;
    
    log_info("xwin: initialized (%dx%d, %d bpp)\n", 
             vga->width, vga->height, vga->bpp);
    
    return 0;
}

void xwin_exit(xdisplay_t* disp) {
    if (disp == NULL) return;

    // 销毁所有窗口
    for (u32 i = 0; i < disp->window_count; i++) {
        if (disp->windows[i] != NULL && disp->windows[i] != disp->root_window) {
            xwin_destroy_window(disp, disp->windows[i]);
        }
    }

    // 销毁根窗口
    if (disp->root_window != NULL) {
        xwin_destroy_window(disp, disp->root_window);
    }

    // 释放资源
    if (disp->event_queue != NULL) {
        queue_pool_destroy(disp->event_queue);
    }
    if (disp->screen_buffer != NULL) {
        kfree(disp->screen_buffer);
    }
    if (disp->back_buffer != NULL) {
        kfree(disp->back_buffer);
    }
    if (disp->windows != NULL) {
        kfree(disp->windows);
    }

    // 清除全局显示服务器
    if (g_display == disp) {
        g_display = NULL;
    }

    g_display = NULL;
    log_info("xwin: exited\n");
}

// ========== 窗口管理 ==========

xwindow_t* xwin_create_window(xdisplay_t* disp, 
                              xwindow_t* parent,
                              i32 x, i32 y, 
                              u32 width, u32 height,
                              u32 flags) {
    if (disp == NULL) return NULL;
    
    // 分配窗口结构
    xwindow_t* win = kmalloc(sizeof(xwindow_t), KERNEL_TYPE);
    if (win == NULL) {
        log_error("xwin: failed to allocate window\n");
        return NULL;
    }
    kmemset(win, 0, sizeof(xwindow_t));
    
    // 设置窗口属性
    win->id = disp->next_window_id++;
    win->x = x;
    win->y = y;
    win->width = width;
    win->height = height;
    win->flags = flags;
    win->visible = (flags & XWIN_FLAG_VISIBLE) ? 1 : 0;
    win->bg_color = XCOLOR_WHITE;
    win->zorder = disp->window_count;
    
    // 计算绝对位置
    if (parent != NULL) {
        win->abs_x = parent->abs_x + x;
        win->abs_y = parent->abs_y + y;
    } else {
        win->abs_x = x;
        win->abs_y = y;
    }
    
    // 分配窗口缓冲区
    win->fb_size = width * height * sizeof(u32);
    win->framebuffer = kmalloc(win->fb_size, KERNEL_TYPE);
    if (win->framebuffer == NULL) {
        log_error("xwin: failed to allocate window framebuffer\n");
        kfree(win);
        return NULL;
    }
    kmemset(win->framebuffer, 0, win->fb_size);
    
    // 设置窗口树
    win->parent = parent;
    if (parent != NULL) {
        if (parent->first_child == NULL) {
            parent->first_child = win;
            parent->last_child = win;
        } else {
            parent->last_child->next_sibling = win;
            win->prev_sibling = parent->last_child;
            parent->last_child = win;
        }
    }
    
    // 添加到窗口数组
    if (disp->window_count < disp->window_capacity) {
        disp->windows[disp->window_count++] = win;
    }
    
    // 发送创建事件
    xevent_t event;
    kmemset(&event, 0, sizeof(event));
    event.type = XEVENT_CREATE;
    event.window_id = win->id;
    xwin_send_event(disp, win, &event);
    
    log_debug("xwin: created window %d (%dx%d at %d,%d)\n", 
              win->id, width, height, x, y);
    
    return win;
}

void xwin_destroy_window(xdisplay_t* disp, xwindow_t* win) {
    if (disp == NULL || win == NULL) return;
    
    // 发送销毁事件
    xevent_t event;
    kmemset(&event, 0, sizeof(event));
    event.type = XEVENT_DESTROY;
    event.window_id = win->id;
    xwin_send_event(disp, win, &event);
    
    // 递归销毁子窗口
    xwindow_t* child = win->first_child;
    while (child != NULL) {
        xwindow_t* next = child->next_sibling;
        xwin_destroy_window(disp, child);
        child = next;
    }
    
    // 从窗口树中移除
    if (win->parent != NULL) {
        if (win->parent->first_child == win) {
            win->parent->first_child = win->next_sibling;
        }
        if (win->parent->last_child == win) {
            win->parent->last_child = win->prev_sibling;
        }
        if (win->prev_sibling != NULL) {
            win->prev_sibling->next_sibling = win->next_sibling;
        }
        if (win->next_sibling != NULL) {
            win->next_sibling->prev_sibling = win->prev_sibling;
        }
    }
    
    // 从窗口数组中移除
    for (u32 i = 0; i < disp->window_count; i++) {
        if (disp->windows[i] == win) {
            // 移动后面的窗口
            for (u32 j = i; j < disp->window_count - 1; j++) {
                disp->windows[j] = disp->windows[j + 1];
            }
            disp->window_count--;
            break;
        }
    }
    
    // 清除焦点
    if (disp->focused_window == win) {
        disp->focused_window = disp->root_window;
    }
    
    // 释放资源
    if (win->framebuffer != NULL) {
        kfree(win->framebuffer);
    }
    kfree(win);
    
    log_debug("xwin: destroyed window %d\n", win->id);
}

xwindow_t* xwin_find_window(xdisplay_t* disp, u32 id) {
    if (disp == NULL) return NULL;
    
    for (u32 i = 0; i < disp->window_count; i++) {
        if (disp->windows[i] != NULL && disp->windows[i]->id == id) {
            return disp->windows[i];
        }
    }
    return NULL;
}

xwindow_t* xwin_find_window_at(xdisplay_t* disp, i32 x, i32 y) {
    if (disp == NULL) return NULL;
    
    // 从顶层窗口开始查找 (逆序遍历)
    for (i32 i = disp->window_count - 1; i >= 0; i--) {
        xwindow_t* win = disp->windows[i];
        if (win != NULL && win->visible) {
            if (x >= win->abs_x && x < (i32)(win->abs_x + win->width) &&
                y >= win->abs_y && y < (i32)(win->abs_y + win->height)) {
                return win;
            }
        }
    }
    return disp->root_window;
}

// ========== 窗口属性 ==========

void xwin_set_title(xwindow_t* win, const char* title) {
    if (win == NULL || title == NULL) return;
    kstrncpy(win->title, title, sizeof(win->title) - 1);
}

void xwin_move(xdisplay_t* disp, xwindow_t* win, i32 x, i32 y) {
    if (win == NULL) return;
    
    i32 old_x = win->abs_x;
    i32 old_y = win->abs_y;
    
    win->x = x;
    win->y = y;
    
    // 更新绝对位置
    if (win->parent != NULL) {
        win->abs_x = win->parent->abs_x + x;
        win->abs_y = win->parent->abs_y + y;
    } else {
        win->abs_x = x;
        win->abs_y = y;
    }
    
    // 递归更新子窗口
    xwindow_t* child = win->first_child;
    while (child != NULL) {
        child->abs_x = win->abs_x + child->x;
        child->abs_y = win->abs_y + child->y;
        // TODO: 递归更新子窗口的子窗口
        child = child->next_sibling;
    }
    
    // 标记损坏区域
    xwin_damage_all(win);
    
    // 发送移动事件
    xevent_t event;
    kmemset(&event, 0, sizeof(event));
    event.type = XEVENT_MOVE;
    event.window_id = win->id;
    event.data.move.x = x;
    event.data.move.y = y;
    event.data.move.old_x = old_x;
    event.data.move.old_y = old_y;
    xwin_send_event(disp, win, &event);
}

void xwin_resize(xdisplay_t* disp, xwindow_t* win, u32 w, u32 h) {
    if (win == NULL || w == 0 || h == 0) return;
    
    u32 old_w = win->width;
    u32 old_h = win->height;
    
    win->width = w;
    win->height = h;
    
    // 重新分配缓冲区
    u32 new_size = w * h * sizeof(u32);
    u32* new_fb = kmalloc(new_size, KERNEL_TYPE);
    if (new_fb != NULL) {
        // 复制旧数据
        u32 copy_w = (w < old_w) ? w : old_w;
        u32 copy_h = (h < old_h) ? h : old_h;
        for (u32 y = 0; y < copy_h; y++) {
            for (u32 x = 0; x < copy_w; x++) {
                new_fb[y * w + x] = win->framebuffer[y * old_w + x];
            }
        }
        kfree(win->framebuffer);
        win->framebuffer = new_fb;
        win->fb_size = new_size;
    }
    
    // 标记损坏
    xwin_damage_all(win);
    
    // 发送大小改变事件
    xevent_t event;
    kmemset(&event, 0, sizeof(event));
    event.type = XEVENT_RESIZE;
    event.window_id = win->id;
    event.data.resize.width = w;
    event.data.resize.height = h;
    event.data.resize.old_width = old_w;
    event.data.resize.old_height = old_h;
    xwin_send_event(disp, win, &event);
}

void xwin_raise(xdisplay_t* disp, xwindow_t* win) {
    if (disp == NULL || win == NULL || win == disp->root_window) return;
    
    // 将窗口移到数组末尾 (顶层)
    u32 idx = 0;
    for (u32 i = 0; i < disp->window_count; i++) {
        if (disp->windows[i] == win) {
            idx = i;
            break;
        }
    }
    
    // 移动到末尾
    for (u32 i = idx; i < disp->window_count - 1; i++) {
        disp->windows[i] = disp->windows[i + 1];
    }
    disp->windows[disp->window_count - 1] = win;
    
    // 更新 zorder
    for (u32 i = 0; i < disp->window_count; i++) {
        disp->windows[i]->zorder = i;
    }
}

void xwin_lower(xdisplay_t* disp, xwindow_t* win) {
    if (disp == NULL || win == NULL || win == disp->root_window) return;
    
    // 将窗口移到数组开头 (底层)
    u32 idx = 0;
    for (u32 i = 0; i < disp->window_count; i++) {
        if (disp->windows[i] == win) {
            idx = i;
            break;
        }
    }
    
    // 移动到开头
    for (i32 i = idx; i > 0; i--) {
        disp->windows[i] = disp->windows[i - 1];
    }
    disp->windows[0] = win;
    
    // 更新 zorder
    for (u32 i = 0; i < disp->window_count; i++) {
        disp->windows[i]->zorder = i;
    }
}

void xwin_show(xdisplay_t* disp, xwindow_t* win, int show) {
    if (win == NULL) return;
    win->visible = show ? 1 : 0;
    if (show) {
        xwin_raise(disp, win);
    }
    xwin_damage_all(win);
}

void xwin_set_bg_color(xwindow_t* win, u32 color) {
    if (win == NULL) return;
    win->bg_color = color;
}

// ========== 焦点管理 ==========

void xwin_set_focus(xdisplay_t* disp, xwindow_t* win) {
    if (disp == NULL || win == NULL) return;
    
    xwindow_t* old_focus = disp->focused_window;
    
    // 发送失去焦点事件
    if (old_focus != NULL && old_focus != win) {
        xevent_t event;
        kmemset(&event, 0, sizeof(event));
        event.type = XEVENT_FOCUS_OUT;
        event.window_id = old_focus->id;
        xwin_send_event(disp, old_focus, &event);
        old_focus->focused = 0;
    }
    
    // 设置新焦点
    disp->focused_window = win;
    win->focused = 1;
    
    // 发送获得焦点事件
    xevent_t event;
    kmemset(&event, 0, sizeof(event));
    event.type = XEVENT_FOCUS_IN;
    event.window_id = win->id;
    xwin_send_event(disp, win, &event);
    
    // 提升窗口
    xwin_raise(disp, win);
}

xwindow_t* xwin_get_focused(xdisplay_t* disp) {
    if (disp == NULL) return NULL;
    return disp->focused_window;
}

// ========== 辅助函数 ==========

int xwin_contains_point(xwindow_t* win, i32 x, i32 y) {
    if (win == NULL) return 0;
    return (x >= win->abs_x && x < (i32)(win->abs_x + win->width) &&
            y >= win->abs_y && y < (i32)(win->abs_y + win->height));
}

void xwin_screen_to_window(xwindow_t* win, i32* x, i32* y) {
    if (win == NULL || x == NULL || y == NULL) return;
    *x -= win->abs_x;
    *y -= win->abs_y;
}

void xwin_window_to_screen(xwindow_t* win, i32* x, i32* y) {
    if (win == NULL || x == NULL || y == NULL) return;
    *x += win->abs_x;
    *y += win->abs_y;
}

void xwin_damage(xwindow_t* win, i32 x, i32 y, u32 w, u32 h) {
    if (win == NULL) return;
    // 简单实现：标记整个窗口为损坏
    win->damaged = 1;
}

void xwin_damage_all(xwindow_t* win) {
    if (win == NULL) return;
    win->damaged = 1;
}
