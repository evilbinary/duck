/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * X Window System - Window Manager
 ********************************************************************/
#include "xwin.h"

#define TITLE_BAR_HEIGHT 24
#define BORDER_WIDTH 2
#define BUTTON_SIZE 16

// ========== 窗口管理器状态 ==========
typedef struct xwm {
    xdisplay_t* display;
    u32 title_bar_color;
    u32 title_bar_active_color;
    u32 border_color;
    u32 border_active_color;
} xwm_t;

static xwm_t g_wm;

// ========== 初始化 ==========

void xwm_init(xdisplay_t* disp) {
    g_wm.display = disp;
    g_wm.title_bar_color = 0xFF606060;
    g_wm.title_bar_active_color = 0xFF4A90D9;
    g_wm.border_color = 0xFF808080;
    g_wm.border_active_color = 0xFF4A90D9;
}

// ========== 窗口装饰绘制 ==========

void xwm_draw_title_bar(xwindow_t* win) {
    if (win == NULL || win->flags & XWIN_FLAG_ROOT) return;
    
    u32 bg_color = win->focused ? g_wm.title_bar_active_color : g_wm.title_bar_color;
    
    // 绘制标题栏背景
    xwin_fill_rect(win, 0, 0, win->width, TITLE_BAR_HEIGHT, bg_color);
    
    // 绘制标题文字
    if (win->title[0] != '\0') {
        xwin_draw_text(win, BORDER_WIDTH + 4, 4, win->title, XCOLOR_WHITE);
    }
    
    // 绘制控制按钮
    u32 btn_x = win->width - BUTTON_SIZE - 4;
    
    // 最小化按钮
    xwin_draw_rect(win, btn_x - BUTTON_SIZE * 2 - 4, 
                   (TITLE_BAR_HEIGHT - BUTTON_SIZE) / 2, 
                   BUTTON_SIZE, BUTTON_SIZE, XCOLOR_LIGHT_GRAY);
    xwin_draw_line(win, btn_x - BUTTON_SIZE * 2 - 4 + 2, 
                   TITLE_BAR_HEIGHT / 2, 
                   btn_x - BUTTON_SIZE - 6 - 2, 
                   TITLE_BAR_HEIGHT / 2, XCOLOR_WHITE);
    
    // 最大化按钮
    xwin_draw_rect(win, btn_x - BUTTON_SIZE - 2, 
                   (TITLE_BAR_HEIGHT - BUTTON_SIZE) / 2, 
                   BUTTON_SIZE, BUTTON_SIZE, XCOLOR_LIGHT_GRAY);
    xwin_draw_rect(win, btn_x - BUTTON_SIZE - 2 + 2, 
                   (TITLE_BAR_HEIGHT - BUTTON_SIZE) / 2 + 2, 
                   BUTTON_SIZE - 4, BUTTON_SIZE - 4, XCOLOR_WHITE);
    
    // 关闭按钮
    xwin_fill_rect(win, btn_x, 
                   (TITLE_BAR_HEIGHT - BUTTON_SIZE) / 2, 
                   BUTTON_SIZE, BUTTON_SIZE, 0xFFCC0000);
    xwin_draw_line(win, btn_x + 2, 
                   (TITLE_BAR_HEIGHT - BUTTON_SIZE) / 2 + 2, 
                   btn_x + BUTTON_SIZE - 2, 
                   (TITLE_BAR_HEIGHT - BUTTON_SIZE) / 2 + BUTTON_SIZE - 2, XCOLOR_WHITE);
    xwin_draw_line(win, btn_x + BUTTON_SIZE - 2, 
                   (TITLE_BAR_HEIGHT - BUTTON_SIZE) / 2 + 2, 
                   btn_x + 2, 
                   (TITLE_BAR_HEIGHT - BUTTON_SIZE) / 2 + BUTTON_SIZE - 2, XCOLOR_WHITE);
}

void xwm_draw_border(xwindow_t* win) {
    if (win == NULL || win->flags & XWIN_FLAG_ROOT) return;
    
    u32 border_color = win->focused ? g_wm.border_active_color : g_wm.border_color;
    
    // 绘制边框
    xwin_draw_rect(win, 0, 0, win->width, win->height, border_color);
    
    // 加粗边框
    xwin_draw_rect(win, 1, 1, win->width - 2, win->height - 2, border_color);
}

void xwm_decorate_window(xwindow_t* win) {
    if (win == NULL || win->flags & XWIN_FLAG_ROOT) return;
    if (!(win->flags & XWIN_FLAG_BORDERED)) return;
    
    xwm_draw_border(win);
    xwm_draw_title_bar(win);
}

// ========== 窗口管理器事件处理 ==========

int xwm_handle_mouse_down(xdisplay_t* disp, xwindow_t* win, i32 x, i32 y, u32 button) {
    if (win == NULL || win->flags & XWIN_FLAG_ROOT) return 0;
    
    log_info("xwm_handle_mouse_down: x=%d y=%d button=%d TITLE_BAR_HEIGHT=%d\n",
              x, y, button, TITLE_BAR_HEIGHT);
    
    // 检查是否点击标题栏
    if (y < TITLE_BAR_HEIGHT && button == XBUTTON_LEFT) {
        log_info("  -> in title bar, DRAGGABLE=%d\n", (win->flags & XWIN_FLAG_DRAGGABLE) ? 1 : 0);
        
        // 检查按钮点击
        u32 btn_x = win->width - BUTTON_SIZE - 4;
        u32 btn_y = (TITLE_BAR_HEIGHT - BUTTON_SIZE) / 2;
        
        // 关闭按钮
        if (x >= btn_x && x < btn_x + BUTTON_SIZE && 
            y >= btn_y && y < btn_y + BUTTON_SIZE) {
            // 发送关闭事件
            xevent_t event;
            kmemset(&event, 0, sizeof(event));
            event.type = XEVENT_CLOSE;
            event.window_id = win->id;
            xwin_send_event(disp, win, &event);
            return 1;
        }
        
        // 最小化按钮
        if (x >= btn_x - BUTTON_SIZE * 2 - 4 && x < btn_x - BUTTON_SIZE - 4 && 
            y >= btn_y && y < btn_y + BUTTON_SIZE) {
            xwin_show(disp, win, 0);
            return 1;
        }
        
        // 最大化按钮
        if (x >= btn_x - BUTTON_SIZE - 2 && x < btn_x - 2 && 
            y >= btn_y && y < btn_y + BUTTON_SIZE) {
            // TODO: 实现最大化
            return 1;
        }
        
        // 标题栏拖拽
        if (win->flags & XWIN_FLAG_DRAGGABLE) {
            disp->drag_window = win;
            disp->drag_offset_x = x;
            disp->drag_offset_y = y;
            log_info("  -> drag started! drag_window=%x\n", disp->drag_window);
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
    
    // 设置标题
    xwin_set_title(win, title);
    
    // 设置背景色
    xwin_set_bg_color(win, XCOLOR_WHITE);
    
    return win;
}

// ========== 桌面绘制 ==========

void xwm_draw_desktop(xdisplay_t* disp) {
    if (disp == NULL || disp->root_window == NULL) return;
    
    xwindow_t* desktop = disp->root_window;
    
    // 绘制桌面背景
    xwin_clear_color(desktop, XCOLOR_DARK_GRAY);
    
    // 可以在这里绘制桌面图标等
}
