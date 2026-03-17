/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * X Window System - Rendering Engine
 ********************************************************************/
#include "xwin.h"

// ========== 鼠标光标 (16x16) ==========
static const u32 cursor_arrow[16][16] = {
    {0xFF000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xFF000000, 0xFF000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xFF000000, 0xFFFFFFFF, 0xFF000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000, 0, 0, 0, 0, 0, 0, 0},
    {0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000, 0xFF000000, 0xFF000000, 0, 0, 0, 0, 0, 0, 0},
    {0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF, 0xFF000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xFF000000, 0xFFFFFFFF, 0xFF000000, 0, 0xFF000000, 0xFF000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xFF000000, 0xFF000000, 0, 0, 0, 0xFF000000, 0xFF000000, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0xFF000000, 0, 0, 0, 0, 0, 0xFF000000, 0xFF000000, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0xFF000000, 0xFF000000, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0xFF000000, 0xFF000000, 0, 0, 0, 0, 0, 0},
};

// ========== 渲染 ==========

void xwin_render(xdisplay_t* disp) {
    if (disp == NULL || disp->vga == NULL) return;
    
    // 清空后备缓冲
    kmemset(disp->back_buffer, 0, disp->buffer_size);
    
    // 合成所有可见窗口 (按zorder从低到高)
    xwin_composite(disp);
    
    // 绘制鼠标光标
    xwin_update_mouse_cursor(disp);
    
    // 翻转缓冲区
    xwin_flip_buffer(disp);
    
    // 更新FPS
    disp->frame_count++;
}

void xwin_render_window(xdisplay_t* disp, xwindow_t* win) {
    if (disp == NULL || win == NULL || !win->visible) return;
    
    // 渲染窗口内容到后备缓冲
    xwin_composite_window(disp, win);
}

void xwin_flip_buffer(xdisplay_t* disp) {
    if (disp == NULL || disp->vga == NULL) return;
    
    // 复制后备缓冲到屏幕缓冲
    kmemcpy(disp->screen_buffer, disp->back_buffer, disp->buffer_size);
    
    // 如果VGA有flip_buffer函数，调用它
    if (disp->vga->flip_buffer != NULL) {
        disp->vga->flip_buffer(disp->vga, 0);
    } else {
        // 直接写入framebuffer
        if (disp->vga->frambuffer != NULL) {
            kmemcpy(disp->vga->frambuffer, disp->back_buffer, disp->buffer_size);
        }
    }
}

void xwin_update_mouse_cursor(xdisplay_t* disp) {
    if (disp == NULL || !disp->mouse_visible) return;
    
    i32 mx = disp->mouse_x;
    i32 my = disp->mouse_y;
    
    // 绘制鼠标光标 (透明色为0)
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            u32 color = cursor_arrow[y][x];
            if (color != 0) {
                i32 px = mx + x;
                i32 py = my + y;
                if (px >= 0 && px < (i32)disp->vga->width && 
                    py >= 0 && py < (i32)disp->vga->height) {
                    disp->back_buffer[py * disp->vga->width + px] = color;
                }
            }
        }
    }
}

// ========== 窗口合成 ==========

void xwin_composite(xdisplay_t* disp) {
    if (disp == NULL || disp->back_buffer == NULL) return;
    
    // 按zorder顺序渲染所有可见窗口
    for (u32 i = 0; i < disp->window_count; i++) {
        xwindow_t* win = disp->windows[i];
        if (win != NULL && win->visible) {
            xwin_composite_window(disp, win);
        }
    }
}

void xwin_composite_window(xdisplay_t* disp, xwindow_t* win) {
    if (disp == NULL || win == NULL || win->framebuffer == NULL) return;
    if (!win->visible) return;
    
    u32* dst = disp->back_buffer;
    u32* src = win->framebuffer;
    
    u32 screen_w = disp->vga->width;
    u32 screen_h = disp->vga->height;
    
    // 计算可见区域
    i32 sx = win->abs_x;
    i32 sy = win->abs_y;
    i32 ex = sx + win->width;
    i32 ey = sy + win->height;
    
    // 裁剪到屏幕范围
    i32 clip_sx = (sx < 0) ? 0 : sx;
    i32 clip_sy = (sy < 0) ? 0 : sy;
    i32 clip_ex = (ex > (i32)screen_w) ? screen_w : ex;
    i32 clip_ey = (ey > (i32)screen_h) ? screen_h : ey;
    
    // 复制像素
    for (i32 y = clip_sy; y < clip_ey; y++) {
        for (i32 x = clip_sx; x < clip_ex; x++) {
            i32 src_x = x - sx;
            i32 src_y = y - sy;
            
            u32 color = src[src_y * win->width + src_x];
            
            // Alpha混合
            u8 alpha = (color >> 24) & 0xFF;
            if (alpha == 255) {
                dst[y * screen_w + x] = color;
            } else if (alpha > 0) {
                u32 dst_color = dst[y * screen_w + x];
                dst[y * screen_w + x] = 
                    ((alpha * ((color >> 16) & 0xFF) + (255 - alpha) * ((dst_color >> 16) & 0xFF)) >> 8) << 16 |
                    ((alpha * ((color >> 8) & 0xFF) + (255 - alpha) * ((dst_color >> 8) & 0xFF)) >> 8) << 8 |
                    ((alpha * (color & 0xFF) + (255 - alpha) * (dst_color & 0xFF)) >> 8) |
                    0xFF000000;
            }
        }
    }
    
    // 如果窗口有边框，绘制边框
    if (win->flags & XWIN_FLAG_BORDERED && win != disp->root_window) {
        // 绘制边框
        u32 border_color = win->focused ? 0xFF4A90D9 : 0xFF808080;
        i32 bw = 2;  // 边框宽度
        
        // 上边框
        for (i32 y = clip_sy; y < clip_sy + bw && y < clip_ey; y++) {
            for (i32 x = clip_sx; x < clip_ex; x++) {
                dst[y * screen_w + x] = border_color;
            }
        }
        
        // 下边框
        for (i32 y = clip_ey - bw; y < clip_ey; y++) {
            for (i32 x = clip_sx; x < clip_ex; x++) {
                dst[y * screen_w + x] = border_color;
            }
        }
        
        // 左边框
        for (i32 y = clip_sy; y < clip_ey; y++) {
            for (i32 x = clip_sx; x < clip_sx + bw && x < clip_ex; x++) {
                dst[y * screen_w + x] = border_color;
            }
        }
        
        // 右边框
        for (i32 y = clip_sy; y < clip_ey; y++) {
            for (i32 x = clip_ex - bw; x < clip_ex; x++) {
                dst[y * screen_w + x] = border_color;
            }
        }
    }
    
    // 清除损坏标记
    win->damaged = 0;
}

// ========== 窗口管理器 (基础) ==========

// 绘制窗口装饰
void xwm_decorate_window(xwindow_t* win) {
    if (win == NULL || !(win->flags & XWIN_FLAG_BORDERED)) return;
    
    u32 title_height = 24;
    u32 title_bg = win->focused ? 0xFF4A90D9 : 0xFF606060;
    
    // 绘制标题栏背景
    xwin_fill_rect(win, 0, 0, win->width, title_height, title_bg);
    
    // 绘制标题文字
    xwin_draw_text(win, 8, 4, win->title, XCOLOR_WHITE);
    
    // 绘制关闭按钮
    xwin_fill_rect(win, win->width - 20, 4, 16, 16, 0xFFCC0000);
    xwin_draw_line(win, win->width - 18, 6, win->width - 6, 18, XCOLOR_WHITE);
    xwin_draw_line(win, win->width - 6, 6, win->width - 18, 18, XCOLOR_WHITE);
}
