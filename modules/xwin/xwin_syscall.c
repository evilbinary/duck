/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * X Window System - Kernel Syscall Handler
 ********************************************************************/
#include "xwin.h"

// ========== Syscall 编号定义 (内核端) ==========
#define SYS_XWIN_BASE          0x5000

#define SYS_XWIN_CREATE        (SYS_XWIN_BASE + 0)
#define SYS_XWIN_DESTROY       (SYS_XWIN_BASE + 1)
#define SYS_XWIN_MOVE          (SYS_XWIN_BASE + 2)
#define SYS_XWIN_RESIZE        (SYS_XWIN_BASE + 3)
#define SYS_XWIN_SHOW          (SYS_XWIN_BASE + 4)
#define SYS_XWIN_SET_TITLE     (SYS_XWIN_BASE + 5)
#define SYS_XWIN_SET_BG_COLOR  (SYS_XWIN_BASE + 6)

#define SYS_XWIN_CLEAR         (SYS_XWIN_BASE + 10)
#define SYS_XWIN_FILL_RECT     (SYS_XWIN_BASE + 11)
#define SYS_XWIN_DRAW_RECT     (SYS_XWIN_BASE + 12)
#define SYS_XWIN_DRAW_LINE     (SYS_XWIN_BASE + 13)
#define SYS_XWIN_DRAW_TEXT     (SYS_XWIN_BASE + 14)

#define SYS_XWIN_GET_EVENT     (SYS_XWIN_BASE + 20)
#define SYS_XWIN_PROCESS_EVENTS (SYS_XWIN_BASE + 21)
#define SYS_XWIN_RENDER        (SYS_XWIN_BASE + 22)
#define SYS_XWIN_UPDATE        (SYS_XWIN_BASE + 23)

// ========== 外部全局显示服务器 ==========
extern xdisplay_t* g_display;

// ========== Syscall 实现函数 ==========

long xwin_syscall_create(long x, long y, long width, long height, long title) {
    xdisplay_t* disp = g_display;
    if (disp == NULL) return 0;
    
    char ktitle[64] = {0};
    const char* utitle = (const char*)title;
    if (utitle != NULL) {
        for (int i = 0; i < 63 && utitle[i] != '\0'; i++) {
            ktitle[i] = utitle[i];
        }
    }
    
    xwindow_t* win = xwin_create_window(disp, disp->root_window, 
                                        (i32)x, (i32)y, 
                                        (u32)width, (u32)height,
                                        XWIN_FLAG_VISIBLE | XWIN_FLAG_BORDERED | 
                                        XWIN_FLAG_DRAGGABLE | XWIN_FLAG_FOCUSABLE);
    if (win == NULL) return 0;
    
    if (ktitle[0] != '\0') {
        xwin_set_title(win, ktitle);
    }
    
    return (long)win->id;
}

long xwin_syscall_destroy(long win_id) {
    xdisplay_t* disp = g_display;
    if (disp == NULL) return -1;
    
    xwindow_t* win = xwin_find_window(disp, (u32)win_id);
    if (win == NULL) return -1;
    
    xwin_destroy_window(disp, win);
    return 0;
}

long xwin_syscall_move(long win_id, long x, long y) {
    xdisplay_t* disp = g_display;
    if (disp == NULL) return -1;
    
    xwindow_t* win = xwin_find_window(disp, (u32)win_id);
    if (win == NULL) return -1;
    
    xwin_move(disp, win, (i32)x, (i32)y);
    return 0;
}

long xwin_syscall_resize(long win_id, long width, long height) {
    xdisplay_t* disp = g_display;
    if (disp == NULL) return -1;
    
    xwindow_t* win = xwin_find_window(disp, (u32)win_id);
    if (win == NULL) return -1;
    
    xwin_resize(disp, win, (u32)width, (u32)height);
    return 0;
}

long xwin_syscall_show(long win_id, long show) {
    xdisplay_t* disp = g_display;
    if (disp == NULL) return -1;
    
    xwindow_t* win = xwin_find_window(disp, (u32)win_id);
    if (win == NULL) return -1;
    
    xwin_show(disp, win, (int)show);
    return 0;
}

long xwin_syscall_set_title(long win_id, long title) {
    xdisplay_t* disp = g_display;
    if (disp == NULL) return -1;
    
    xwindow_t* win = xwin_find_window(disp, (u32)win_id);
    if (win == NULL) return -1;
    
    if (title != 0) {
        xwin_set_title(win, (const char*)title);
    }
    return 0;
}

long xwin_syscall_set_bg_color(long win_id, long color) {
    xdisplay_t* disp = g_display;
    if (disp == NULL) return -1;
    
    xwindow_t* win = xwin_find_window(disp, (u32)win_id);
    if (win == NULL) return -1;
    
    xwin_set_bg_color(win, (u32)color);
    return 0;
}

long xwin_syscall_clear(long win_id) {
    xdisplay_t* disp = g_display;
    if (disp == NULL) return -1;
    
    xwindow_t* win = xwin_find_window(disp, (u32)win_id);
    if (win == NULL) return -1;
    
    xwin_clear(win);
    return 0;
}

long xwin_syscall_fill_rect(long win_id, long x, long y, long wh, long color) {
    xdisplay_t* disp = g_display;
    if (disp == NULL) return -1;
    
    xwindow_t* win = xwin_find_window(disp, (u32)win_id);
    if (win == NULL) return -1;
    
    u32 w = wh & 0xFFFF;
    u32 h = (wh >> 16) & 0xFFFF;
    xwin_fill_rect(win, (i32)x, (i32)y, w, h, (u32)color);
    return 0;
}

long xwin_syscall_draw_rect(long win_id, long x, long y, long wh, long color) {
    xdisplay_t* disp = g_display;
    if (disp == NULL) return -1;
    
    xwindow_t* win = xwin_find_window(disp, (u32)win_id);
    if (win == NULL) return -1;
    
    u32 w = wh & 0xFFFF;
    u32 h = (wh >> 16) & 0xFFFF;
    xwin_draw_rect(win, (i32)x, (i32)y, w, h, (u32)color);
    return 0;
}

long xwin_syscall_draw_line(long win_id, long xy1, long xy2, long color, long unused) {
    xdisplay_t* disp = g_display;
    if (disp == NULL) return -1;
    
    xwindow_t* win = xwin_find_window(disp, (u32)win_id);
    if (win == NULL) return -1;
    
    i32 x1 = xy1 & 0xFFFF;
    i32 y1 = (xy1 >> 16) & 0xFFFF;
    i32 x2 = xy2 & 0xFFFF;
    i32 y2 = (xy2 >> 16) & 0xFFFF;
    
    xwin_draw_line(win, x1, y1, x2, y2, (u32)color);
    return 0;
}

long xwin_syscall_draw_text(long win_id, long x, long y, long text, long color) {
    xdisplay_t* disp = g_display;
    if (disp == NULL) return -1;
    
    xwindow_t* win = xwin_find_window(disp, (u32)win_id);
    if (win == NULL) return -1;
    
    if (text != 0) {
        xwin_draw_text(win, (i32)x, (i32)y, (const char*)text, (u32)color);
    }
    return 0;
}

long xwin_syscall_get_event(long event_ptr) {
    xdisplay_t* disp = g_display;
    if (disp == NULL) return -1;
    
    xinput_poll();

    xevent_t event;
    if (xwin_next_event(disp, &event)) {
        if (event_ptr != 0) {
            kmemcpy((void*)event_ptr, &event, sizeof(xevent_t));
        }
        return 1;
    }
    return 0;
}

long xwin_syscall_process_events(void) {
    xdisplay_t* disp = g_display;
    if (disp == NULL) return -1;
    
    xinput_poll();
    xwin_process_events(disp);
    return 0;
}

long xwin_syscall_render(void) {
    xdisplay_t* disp = g_display;
    if (disp == NULL) return -1;
    
    xinput_poll();
    xwin_render(disp);
    return 0;
}

long xwin_syscall_update(long win_id) {
    xdisplay_t* disp = g_display;
    if (disp == NULL) return -1;
    
    xwindow_t* win = xwin_find_window(disp, (u32)win_id);
    if (win == NULL) return -1;
    
    xwin_damage_all(win);
    return 0;
}

// ========== Syscall 分发器 ==========

long xwin_syscall_handler(u32 num, long a1, long a2, long a3, long a4, long a5) {
    switch (num) {
        case SYS_XWIN_CREATE:
            return xwin_syscall_create(a1, a2, a3, a4, a5);
        case SYS_XWIN_DESTROY:
            return xwin_syscall_destroy(a1);
        case SYS_XWIN_MOVE:
            return xwin_syscall_move(a1, a2, a3);
        case SYS_XWIN_RESIZE:
            return xwin_syscall_resize(a1, a2, a3);
        case SYS_XWIN_SHOW:
            return xwin_syscall_show(a1, a2);
        case SYS_XWIN_SET_TITLE:
            return xwin_syscall_set_title(a1, a2);
        case SYS_XWIN_SET_BG_COLOR:
            return xwin_syscall_set_bg_color(a1, a2);
        case SYS_XWIN_CLEAR:
            return xwin_syscall_clear(a1);
        case SYS_XWIN_FILL_RECT:
            return xwin_syscall_fill_rect(a1, a2, a3, a4, a5);
        case SYS_XWIN_DRAW_RECT:
            return xwin_syscall_draw_rect(a1, a2, a3, a4, a5);
        case SYS_XWIN_DRAW_LINE:
            return xwin_syscall_draw_line(a1, a2, a3, a4, a5);
        case SYS_XWIN_DRAW_TEXT:
            return xwin_syscall_draw_text(a1, a2, a3, a4, a5);
        case SYS_XWIN_GET_EVENT:
            return xwin_syscall_get_event(a1);
        case SYS_XWIN_PROCESS_EVENTS:
            return xwin_syscall_process_events();
        case SYS_XWIN_RENDER:
            return xwin_syscall_render();
        case SYS_XWIN_UPDATE:
            return xwin_syscall_update(a1);
        default:
            return -1;
    }
}

// ========== 注册 Syscall ==========

void xwin_register_syscall(void) {
    // 注册 syscall 处理器
    // syscall_register(SYS_XWIN_BASE, xwin_syscall_handler);
    log_info("xwin: syscall registered at 0x%x\n", SYS_XWIN_BASE);
}
