/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * X Window System - Window Manager
 ********************************************************************/
#include "xwin.h"

// ========== 窗口管理器状态 ==========
typedef struct xwm {
    xdisplay_t* display;
} xwm_t;

static xwm_t g_wm;

// ========== 初始化 ==========

void xwm_init(xdisplay_t* disp) {
    g_wm.display = disp;
    // 主题由 display 管理
}

// ========== 窗口装饰绘制 ==========

void xwm_draw_title_bar(xwindow_t* win) {
    if (win == NULL || win->flags & XWIN_FLAG_ROOT) return;
    
    // 标题栏由合成器在 xtheme_render_decoration 中绘制
    // 这里不再在窗口 framebuffer 上绘制，避免重复绘制和与边框冲突
}

void xwm_draw_border(xwindow_t* win) {
    if (win == NULL || win->flags & XWIN_FLAG_ROOT) return;
    
    // 边框由合成器在 xtheme_render_decoration 中绘制
    // 这里不再在窗口 framebuffer 上绘制，避免重复绘制和与标题栏冲突
}

void xwm_decorate_window(xwindow_t* win) {
    if (win == NULL || win->flags & XWIN_FLAG_ROOT) return;
    if (!(win->flags & XWIN_FLAG_BORDERED)) return;
    
    // xwm_draw_border(win);
    // xwm_draw_title_bar(win);
}

// ========== 窗口管理器事件处理 ==========

int xwm_handle_mouse_down(xdisplay_t* disp, xwindow_t* win, i32 x, i32 y, u32 button) {
    if (win == NULL || win->flags & XWIN_FLAG_ROOT) return 0;
    
    xtheme_t* t = xtheme_current(disp);
    u32 title_h = t->title_bar_height;
    u32 btn_size = t->btn_size;
    u32 btn_gap = t->btn_gap;
    
    log_info("xwm_handle_mouse_down: x=%d y=%d button=%d title_h=%d\n",
              x, y, button, title_h);
    
    // 检查是否点击标题栏
    if (y < (i32)title_h && button == XBUTTON_LEFT) {
        log_info("  -> in title bar, DRAGGABLE=%d\n", (win->flags & XWIN_FLAG_DRAGGABLE) ? 1 : 0);
        
        // 按钮区域检测
        u32 btn_y = (title_h - btn_size) / 2;
        u32 btn_x = win->width - btn_size - btn_gap;
        u32 btn_cx = btn_x + btn_size / 2;
        u32 btn_cy = btn_y + btn_size / 2;
        u32 btn_radius_sq = (btn_size * btn_size) / 4;
        
        // 根据按钮样式检测
        int in_btn = 0;
        if (t->button_style == XBUTTON_STYLE_CIRCLE) {
            // 圆形按钮检测
            i32 dx = x - (i32)btn_cx;
            i32 dy = y - (i32)btn_cy;
            in_btn = (dx * dx + dy * dy <= (i32)btn_radius_sq);
        } else {
            // 方形按钮检测
            in_btn = (x >= (i32)btn_x && x < (i32)(btn_x + btn_size) &&
                      y >= (i32)btn_y && y < (i32)(btn_y + btn_size));
        }
        
        // 关闭按钮
        if (in_btn) {
            xevent_t event;
            kmemset(&event, 0, sizeof(event));
            event.type = XEVENT_CLOSE;
            event.window_id = win->id;
            xwin_send_event(disp, win, &event);
            return 1;
        }
        
        // 最大化按钮
        btn_cx -= btn_size + btn_gap;
        btn_x -= btn_size + btn_gap;
        if (t->button_style == XBUTTON_STYLE_CIRCLE) {
            i32 dx = x - (i32)btn_cx;
            i32 dy = y - (i32)btn_cy;
            in_btn = (dx * dx + dy * dy <= (i32)btn_radius_sq);
        } else {
            in_btn = (x >= (i32)btn_x && x < (i32)(btn_x + btn_size) &&
                      y >= (i32)btn_y && y < (i32)(btn_y + btn_size));
        }
        if (in_btn) {
            // TODO: 实现最大化
            return 1;
        }
        
        // 最小化按钮
        btn_cx -= btn_size + btn_gap;
        btn_x -= btn_size + btn_gap;
        if (t->button_style == XBUTTON_STYLE_CIRCLE) {
            i32 dx = x - (i32)btn_cx;
            i32 dy = y - (i32)btn_cy;
            in_btn = (dx * dx + dy * dy <= (i32)btn_radius_sq);
        } else {
            in_btn = (x >= (i32)btn_x && x < (i32)(btn_x + btn_size) &&
                      y >= (i32)btn_y && y < (i32)(btn_y + btn_size));
        }
        if (in_btn) {
            xwin_show(disp, win, 0);
            return 1;
        }
        
        // 标题栏拖拽
        if (win->flags & XWIN_FLAG_DRAGGABLE) {
            if (disp->drag_window != NULL && disp->drag_window != win) {
                log_info("  -> ending previous drag on %x\n", disp->drag_window);
            }
            disp->drag_window = win;
            disp->drag_offset_x = x;
            disp->drag_offset_y = y;
            log_info("  -> drag started! drag_window=%x offset=(%d,%d)\n",
                     disp->drag_window, disp->drag_offset_x, disp->drag_offset_y);
            return 1;
        }
    }
    
    return 0;
}

int xwm_handle_mouse_up(xdisplay_t* disp, xwindow_t* win, i32 x, i32 y, u32 button) {
    if (disp->drag_window != NULL) {
        disp->drag_window = NULL;
        return 1;
    }
    return 0;
}

// ========== 创建标准窗口 ==========

xwindow_t* xwm_create_standard_window(xdisplay_t* disp, 
                                      i32 x, i32 y, 
                                      u32 width, u32 height,
                                      const char* title) {
    xwindow_t* win = xwin_create_window(disp, disp->root_window, 
                                        x, y, width, height,
                                        XWIN_FLAG_VISIBLE | 
                                        XWIN_FLAG_BORDERED | 
                                        XWIN_FLAG_DRAGGABLE |
                                        XWIN_FLAG_FOCUSABLE);
    if (win == NULL) return NULL;
    
    xwin_set_title(win, title);
    xwin_set_bg_color(win, XCOLOR_WHITE);
    
    return win;
}

// ========== 桌面绘制 ==========

void xwm_draw_desktop(xdisplay_t* disp) {
    if (disp == NULL || disp->root_window == NULL) return;
    
    xwindow_t* desktop = disp->root_window;
    xtheme_t* t = xtheme_current(disp);
    
    // 使用主题的桌面背景
    u32 bg_top = t->desktop_bg_top;
    u32 bg_bottom = t->desktop_bg_bottom;
    
    // 绘制渐变背景
    for (u32 y = 0; y < desktop->height; y++) {
        u32 ratio = y * 256 / desktop->height;
        u32 r = ((bg_top >> 16 & 0xFF) * (256 - ratio) + (bg_bottom >> 16 & 0xFF) * ratio) >> 8;
        u32 g = ((bg_top >> 8 & 0xFF) * (256 - ratio) + (bg_bottom >> 8 & 0xFF) * ratio) >> 8;
        u32 b = ((bg_top & 0xFF) * (256 - ratio) + (bg_bottom & 0xFF) * ratio) >> 8;
        u32 color = 0xFF000000 | (r << 16) | (g << 8) | b;
        xwin_fill_rect(desktop, 0, y, desktop->width, 1, color);
    }
}
