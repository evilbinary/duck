/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * X Window System - User Space API
 ********************************************************************/
#ifndef XWIN_USER_H
#define XWIN_USER_H

#include "types.h"

// ========== 用户空间窗口句柄 ==========
typedef u32 xwin_handle_t;
#define XWIN_INVALID_HANDLE  0

#define XWIN_FLAG_VISIBLE    0x01
#define XWIN_FLAG_FOCUSABLE  0x02
#define XWIN_FLAG_BORDERED   0x04
#define XWIN_FLAG_DRAGGABLE  0x08
#define XWIN_FLAG_RESIZABLE  0x10
#define XWIN_FLAG_DIRECT     0x20

// ========== 用户空间 API (通过 syscall 调用) ==========

// 窗口管理
xwin_handle_t xwin_create(i32 x, i32 y, u32 width, u32 height, const char* title);
xwin_handle_t xwin_create_flags(i32 x, i32 y, u32 width, u32 height,
                                const char* title, u32 flags);
void xwin_destroy(xwin_handle_t win);
void xwin_move(xwin_handle_t win, i32 x, i32 y);
void xwin_resize(xwin_handle_t win, u32 width, u32 height);
void xwin_show(xwin_handle_t win, int show);
void xwin_set_title(xwin_handle_t win, const char* title);
void xwin_set_bg_color(xwin_handle_t win, u32 color);

// 绘图
void xwin_clear(xwin_handle_t win);
void xwin_fill_rect(xwin_handle_t win, i32 x, i32 y, u32 w, u32 h, u32 color);
void xwin_draw_rect(xwin_handle_t win, i32 x, i32 y, u32 w, u32 h, u32 color);
void xwin_draw_line(xwin_handle_t win, i32 x1, i32 y1, i32 x2, i32 y2, u32 color);
void xwin_draw_text(xwin_handle_t win, i32 x, i32 y, const char* text, u32 color);
void xwin_blit(xwin_handle_t win, i32 x, i32 y, const u32* data, u32 w, u32 h);

// 事件
int xwin_get_event(void* event);
void xwin_process_events(void);

// 渲染
void xwin_render(void);
void xwin_update(xwin_handle_t win);

// ========== Syscall 编号 ==========
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
#define SYS_XWIN_BLIT          (SYS_XWIN_BASE + 15)

#define SYS_XWIN_GET_EVENT     (SYS_XWIN_BASE + 20)
#define SYS_XWIN_PROCESS_EVENTS (SYS_XWIN_BASE + 21)
#define SYS_XWIN_RENDER        (SYS_XWIN_BASE + 22)
#define SYS_XWIN_UPDATE        (SYS_XWIN_BASE + 23)
#define SYS_XWIN_GET_FB        (SYS_XWIN_BASE + 24)
#define SYS_XWIN_GET_TICKS     (SYS_XWIN_BASE + 25)

// ========== 用户空间实现 (示例) ==========

#ifdef XWIN_USER_IMPL

static inline long xwin_syscall(int num, long a1, long a2, long a3, long a4,
                                long a5, long a6) {
#if defined(__aarch64__) || defined(ARM64)
    register long x8 __asm__("x8") = num;
    register long x0 __asm__("x0") = a1;
    register long x1 __asm__("x1") = a2;
    register long x2 __asm__("x2") = a3;
    register long x3 __asm__("x3") = a4;
    register long x4 __asm__("x4") = a5;
    register long x5 __asm__("x5") = a6;
    __asm__ volatile("svc #0"
                     : "+r"(x0)
                     : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
                     : "memory");
    return x0;
#elif defined(__arm__) || defined(ARM)
    register long r7 __asm__("r7") = num;
    register long r0 __asm__("r0") = a1;
    register long r1 __asm__("r1") = a2;
    register long r2 __asm__("r2") = a3;
    register long r3 __asm__("r3") = a4;
    register long r4 __asm__("r4") = a5;
    register long r5 __asm__("r5") = a6;
    __asm__ volatile("svc #0"
                     : "+r"(r0)
                     : "r"(r7), "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5)
                     : "memory");
    return r0;
#else
    #error "Unsupported architecture for xwin_syscall"
#endif
}

static inline xwin_handle_t xwin_create_flags(i32 x, i32 y, u32 width, u32 height,
                                              const char* title, u32 flags) {
    return (xwin_handle_t)xwin_syscall(SYS_XWIN_CREATE, (long)x, (long)y,
                                       (long)width, (long)height, (long)flags,
                                       (long)title);
}

static inline xwin_handle_t xwin_create(i32 x, i32 y, u32 width, u32 height, const char* title) {
    return xwin_create_flags(x, y, width, height, title, 0);
}

static inline void xwin_destroy(xwin_handle_t win) {
    xwin_syscall(SYS_XWIN_DESTROY, (long)win, 0, 0, 0, 0, 0);
}

static inline void xwin_move(xwin_handle_t win, i32 x, i32 y) {
    xwin_syscall(SYS_XWIN_MOVE, (long)win, (long)x, (long)y, 0, 0, 0);
}

static inline void xwin_resize(xwin_handle_t win, u32 width, u32 height) {
    xwin_syscall(SYS_XWIN_RESIZE, (long)win, (long)width, (long)height, 0, 0, 0);
}

static inline void xwin_show(xwin_handle_t win, int show) {
    xwin_syscall(SYS_XWIN_SHOW, (long)win, (long)show, 0, 0, 0, 0);
}

static inline void xwin_set_title(xwin_handle_t win, const char* title) {
    xwin_syscall(SYS_XWIN_SET_TITLE, (long)win, (long)title, 0, 0, 0, 0);
}

static inline void xwin_set_bg_color(xwin_handle_t win, u32 color) {
    xwin_syscall(SYS_XWIN_SET_BG_COLOR, (long)win, (long)color, 0, 0, 0, 0);
}

static inline void xwin_clear(xwin_handle_t win) {
    xwin_syscall(SYS_XWIN_CLEAR, (long)win, 0, 0, 0, 0, 0);
}

static inline void xwin_fill_rect(xwin_handle_t win, i32 x, i32 y, u32 w, u32 h, u32 color) {
    xwin_syscall(SYS_XWIN_FILL_RECT, (long)win, (long)x, (long)y, (long)w | ((long)h << 16), (long)color, 0);
}

static inline void xwin_draw_rect(xwin_handle_t win, i32 x, i32 y, u32 w, u32 h, u32 color) {
    xwin_syscall(SYS_XWIN_DRAW_RECT, (long)win, (long)x, (long)y, (long)w | ((long)h << 16), (long)color, 0);
}

static inline void xwin_draw_line(xwin_handle_t win, i32 x1, i32 y1, i32 x2, i32 y2, u32 color) {
    xwin_syscall(SYS_XWIN_DRAW_LINE, (long)win, 
        (long)x1 | ((long)y1 << 16), (long)x2 | ((long)y2 << 16), (long)color, 0, 0);
}

static inline void xwin_draw_text(xwin_handle_t win, i32 x, i32 y, const char* text, u32 color) {
    xwin_syscall(SYS_XWIN_DRAW_TEXT, (long)win, (long)x, (long)y, (long)text, (long)color, 0);
}

static inline void xwin_blit(xwin_handle_t win, i32 x, i32 y, const u32* data, u32 w, u32 h) {
    xwin_syscall(SYS_XWIN_BLIT, (long)win, (long)x, (long)y,
                 (long)w | ((long)h << 16), (long)data, 0);
}

static inline int xwin_get_event(void* event) {
    return (int)xwin_syscall(SYS_XWIN_GET_EVENT, (long)event, 0, 0, 0, 0, 0);
}

static inline void xwin_process_events(void) {
    xwin_syscall(SYS_XWIN_PROCESS_EVENTS, 0, 0, 0, 0, 0, 0);
}

static inline void xwin_render(void) {
    xwin_syscall(SYS_XWIN_RENDER, 0, 0, 0, 0, 0, 0);
}

static inline void xwin_update(xwin_handle_t win) {
    xwin_syscall(SYS_XWIN_UPDATE, (long)win, 0, 0, 0, 0, 0);
}

static inline u32* xwin_get_fb(xwin_handle_t win) {
    return (u32*)(uintptr_t)xwin_syscall(SYS_XWIN_GET_FB, (long)win, 0, 0, 0, 0,
                                         0);
}

static inline u32 xwin_get_ticks(void) {
    return (u32)xwin_syscall(SYS_XWIN_GET_TICKS, 0, 0, 0, 0, 0, 0);
}

#endif // XWIN_USER_IMPL

#endif // XWIN_USER_H
