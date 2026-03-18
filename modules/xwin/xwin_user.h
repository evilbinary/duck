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

// ========== 用户空间 API (通过 syscall 调用) ==========

// 窗口管理
xwin_handle_t xwin_create(i32 x, i32 y, u32 width, u32 height, const char* title);
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

#define SYS_XWIN_GET_EVENT     (SYS_XWIN_BASE + 20)
#define SYS_XWIN_PROCESS_EVENTS (SYS_XWIN_BASE + 21)
#define SYS_XWIN_RENDER        (SYS_XWIN_BASE + 22)
#define SYS_XWIN_UPDATE        (SYS_XWIN_BASE + 23)

// ========== 用户空间实现 (示例) ==========

#ifdef XWIN_USER_IMPL

// Syscall 包装函数
static inline long xwin_syscall(int num, long a1, long a2, long a3, long a4, long a5) {
    long ret;
#if defined(__aarch64__) || defined(ARM64)
    __asm__ volatile (
        "mov x8, %1\n"
        "mov x0, %2\n"
        "mov x1, %3\n"
        "mov x2, %4\n"
        "mov x3, %5\n"
        "mov x4, %6\n"
        "svc #0\n"
        "mov %0, x0\n"
        : "=r"(ret)
        : "r"(num), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5)
        : "x0", "x1", "x2", "x3", "x4", "x8"
    );
#elif defined(__arm__) || defined(ARM)
    __asm__ volatile (
        "mov r7, %1\n"
        "mov r0, %2\n"
        "mov r1, %3\n"
        "mov r2, %4\n"
        "mov r3, %5\n"
        "mov r4, %6\n"
        "svc #0\n"
        "mov %0, r0\n"
        : "=r"(ret)
        : "r"(num), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5)
        : "r0", "r1", "r2", "r3", "r4", "r7"
    );
#else
    #error "Unsupported architecture for xwin_syscall"
#endif
    return ret;
}

xwin_handle_t xwin_create(i32 x, i32 y, u32 width, u32 height, const char* title) {
    return (xwin_handle_t)xwin_syscall(SYS_XWIN_CREATE, 
        (long)x, (long)y, (long)width, (long)height, (long)title);
}

void xwin_destroy(xwin_handle_t win) {
    xwin_syscall(SYS_XWIN_DESTROY, (long)win, 0, 0, 0, 0);
}

void xwin_move(xwin_handle_t win, i32 x, i32 y) {
    xwin_syscall(SYS_XWIN_MOVE, (long)win, (long)x, (long)y, 0, 0);
}

void xwin_resize(xwin_handle_t win, u32 width, u32 height) {
    xwin_syscall(SYS_XWIN_RESIZE, (long)win, (long)width, (long)height, 0, 0);
}

void xwin_show(xwin_handle_t win, int show) {
    xwin_syscall(SYS_XWIN_SHOW, (long)win, (long)show, 0, 0, 0);
}

void xwin_set_title(xwin_handle_t win, const char* title) {
    xwin_syscall(SYS_XWIN_SET_TITLE, (long)win, (long)title, 0, 0, 0);
}

void xwin_set_bg_color(xwin_handle_t win, u32 color) {
    xwin_syscall(SYS_XWIN_SET_BG_COLOR, (long)win, (long)color, 0, 0, 0);
}

void xwin_clear(xwin_handle_t win) {
    xwin_syscall(SYS_XWIN_CLEAR, (long)win, 0, 0, 0, 0);
}

void xwin_fill_rect(xwin_handle_t win, i32 x, i32 y, u32 w, u32 h, u32 color) {
    xwin_syscall(SYS_XWIN_FILL_RECT, (long)win, (long)x, (long)y, (long)w | ((long)h << 16), (long)color);
}

void xwin_draw_rect(xwin_handle_t win, i32 x, i32 y, u32 w, u32 h, u32 color) {
    xwin_syscall(SYS_XWIN_DRAW_RECT, (long)win, (long)x, (long)y, (long)w | ((long)h << 16), (long)color);
}

void xwin_draw_line(xwin_handle_t win, i32 x1, i32 y1, i32 x2, i32 y2, u32 color) {
    xwin_syscall(SYS_XWIN_DRAW_LINE, (long)win, 
        (long)x1 | ((long)y1 << 16), (long)x2 | ((long)y2 << 16), (long)color, 0);
}

void xwin_draw_text(xwin_handle_t win, i32 x, i32 y, const char* text, u32 color) {
    xwin_syscall(SYS_XWIN_DRAW_TEXT, (long)win, (long)x, (long)y, (long)text, (long)color);
}

int xwin_get_event(void* event) {
    return (int)xwin_syscall(SYS_XWIN_GET_EVENT, (long)event, 0, 0, 0, 0);
}

void xwin_process_events(void) {
    xwin_syscall(SYS_XWIN_PROCESS_EVENTS, 0, 0, 0, 0, 0);
}

void xwin_render(void) {
    xwin_syscall(SYS_XWIN_RENDER, 0, 0, 0, 0, 0);
}

void xwin_update(xwin_handle_t win) {
    xwin_syscall(SYS_XWIN_UPDATE, (long)win, 0, 0, 0, 0);
}

#endif // XWIN_USER_IMPL

#endif // XWIN_USER_H
