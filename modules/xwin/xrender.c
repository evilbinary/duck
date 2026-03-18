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
    
    extern u32 schedule_get_ticks(void);
    u32 t0, t1;
    static u32 t_clear = 0, t_composite = 0, t_cursor = 0, t_flip = 0;
    static u32 frame_count = 0;
    static int printed_info = 0;
    
    // 只打印一次分辨率信息
    if (!printed_info) {
        log_info("xwin: %dx%d, buffer=%d bytes, %d KB\n", 
                 disp->vga->width, disp->vga->height, 
                 disp->buffer_size, disp->buffer_size / 1024);
        printed_info = 1;
    }
    
    // 清空后备缓冲
    t0 = schedule_get_ticks();
    kmemset(disp->back_buffer, 0, disp->buffer_size);
    t1 = schedule_get_ticks();
    t_clear += (t1 - t0);
    
    // 合成所有可见窗口 (按zorder从低到高)
    t0 = schedule_get_ticks();
    xwin_composite(disp);
    t1 = schedule_get_ticks();
    t_composite += (t1 - t0);
    
    // 绘制鼠标光标
    t0 = schedule_get_ticks();
    xwin_update_mouse_cursor(disp);
    t1 = schedule_get_ticks();
    t_cursor += (t1 - t0);
    
    // 翻转缓冲区
    t0 = schedule_get_ticks();
    xwin_flip_buffer(disp);
    t1 = schedule_get_ticks();
    t_flip += (t1 - t0);
    
    // 更新FPS
    disp->frame_count++;
    frame_count++;
    
    // 每 60 帧打印一次
    if (frame_count >= 60) {
        log_info("Render: clear=%d composite=%d cursor=%d flip=%d (ticks)\n",
                 t_clear, t_composite, t_cursor, t_flip);
        t_clear = t_composite = t_cursor = t_flip = 0;
        frame_count = 0;
    }
}

void xwin_render_window(xdisplay_t* disp, xwindow_t* win) {
    if (disp == NULL || win == NULL || !win->visible) return;
    
    // 渲染窗口内容到后备缓冲
    xwin_composite_window(disp, win);
}

void xwin_flip_buffer(xdisplay_t* disp) {
    if (disp == NULL || disp->vga == NULL) return;

    // 如果VGA有flip_buffer函数，调用它（由驱动处理显示）
    if (disp->vga->flip_buffer != NULL) {
        disp->vga->flip_buffer(disp->vga, 0);
    } else if (disp->vga->frambuffer != NULL) {
        // 直接写入framebuffer，不需要中间的screen_buffer
        kmemcpy(disp->vga->frambuffer, disp->back_buffer, disp->buffer_size);
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
    
    i32 copy_width = clip_ex - clip_sx;
    if (copy_width <= 0) return;
    
    // 优化：按行复制，避免逐像素计算
    for (i32 y = clip_sy; y < clip_ey; y++) {
        i32 src_y = y - sy;
        i32 src_x = clip_sx - sx;
        
        u32* dst_row = dst + y * screen_w + clip_sx;
        u32* src_row = src + src_y * win->width + src_x;
        
        // 快速路径：直接复制整行（假设大部分情况不需要 alpha 混合）
        kmemcpy(dst_row, src_row, copy_width * sizeof(u32));
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
