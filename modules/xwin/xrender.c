/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * X Window System - Rendering Engine
 ********************************************************************/
#include "xwin.h"
#include "font.h"
#include "kernel/page.h"
#include "kernel/thread.h"
#include "kernel/memory.h"
#include "kernel/config.h"
#include "arch/cpu.h"

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
    static u32 t_kernel = 0; /* 仅内核 render 路径累计 tick */
    static u32 frame_count = 0;
    static u32 skip_count = 0;
    static u32 fps_t0 = 0;
    static int printed_info = 0;
    
    // 只打印一次分辨率信息
    if (!printed_info) {
        log_info("xwin: %dx%d, buffer=%d bytes, %d KB\n", 
                 disp->vga->width, disp->vga->height, 
                 disp->buffer_size, disp->buffer_size / 1024);
        log_info("xwin: stats: app_fps=墙钟(含用户态); k_us=内核render均耗\n");
        printed_info = 1;
        fps_t0 = schedule_get_ticks();
    }
    
    // 检查是否有窗口需要更新
    int need_render = 0;
    for (u32 i = 0; i < disp->window_count; i++) {
        xwindow_t* win = disp->windows[i];
        if (win != NULL && win->visible && win->damaged) {
            need_render = 1;
            break;
        }
    }
    
    // 鼠标移动也需要更新
    static i32 last_mouse_x = -1, last_mouse_y = -1;
    if (disp->mouse_x != last_mouse_x || disp->mouse_y != last_mouse_y) {
        need_render = 1;
        last_mouse_x = disp->mouse_x;
        last_mouse_y = disp->mouse_y;
    }
    
    // 没有变化则跳过渲染
    if (!need_render) {
        skip_count++;
        return;
    }

    u32 k0 = schedule_get_ticks();

    /* 全屏覆盖窗：只走该窗（可无 DIRECT 标志），跳过根窗 + 多余 memcpy */
    xwindow_t* direct_win = NULL;
    for (u32 i = 0; i < disp->window_count; i++) {
        xwindow_t* win = disp->windows[i];
        if (win == NULL || !win->visible || !win->damaged) continue;
        if (win == disp->root_window) continue;
        if (win->width == disp->vga->width &&
            win->height == disp->vga->height && win->abs_x == 0 &&
            win->abs_y == 0) {
            direct_win = win;
            break;
        }
    }
    if (direct_win != NULL) {
        u32* fb = disp->vga->frambuffer != NULL
                      ? (u32*)disp->vga->frambuffer
                      : NULL;

        t0 = schedule_get_ticks();
        if (fb != NULL && direct_win->framebuffer != fb) {
            /* 用户态应已通过 get_fb 拿到 LCD；此处只补绑，勿再合成离屏 */
            if (xwin_bind_lcd(disp, direct_win) != NULL) {
                log_info("xwin: late DIRECT bind LCD %x\n",
                         (u32)(uintptr_t)fb);
            }
        }
        t1 = schedule_get_ticks();
        t_composite += (t1 - t0);

        t0 = schedule_get_ticks();
        xwin_flip_buffer(disp);
        t1 = schedule_get_ticks();
        t_flip += (t1 - t0);

        for (u32 i = 0; i < disp->window_count; i++) {
            if (disp->windows[i] != NULL) {
                disp->windows[i]->damaged = 0;
            }
        }
        t_kernel += schedule_get_ticks() - k0;
        disp->frame_count++;
        frame_count++;
        if (frame_count >= 60) {
            u32 now = schedule_get_ticks();
            u32 dt = now - fps_t0;
            /* app_fps：两次统计间隔的墙钟帧率（含 fill/blit/sleep 等）
             * k_us：内核本路径均耗；composite=flip=0 时内核不是瓶颈 */
            u32 app_fps = dt > 0 ? (frame_count * SCHEDULE_FREQUENCY) / dt : 0;
            u32 k_us = (t_kernel * 1000) / frame_count; /* tick=ms@1kHz → us */
            log_info("Render: direct-lcd app_fps=%d k_us=%d "
                     "composite=%d flip=%d (skip=%d)\n",
                     app_fps, k_us, t_composite, t_flip, skip_count);
            t_clear = t_composite = t_cursor = t_flip = t_kernel = 0;
            frame_count = 0;
            skip_count = 0;
            fps_t0 = now;
        }
        return;
    }
    
    // 清空后备缓冲 - 只有在没有全屏根窗口时才需要
    // 根窗口会覆盖整个屏幕，所以可以跳过清空
    int need_clear = 1;
    if (disp->root_window != NULL && 
        disp->root_window->visible &&
        disp->root_window->width == disp->vga->width &&
        disp->root_window->height == disp->vga->height) {
        need_clear = 0;
    }
    
    if (need_clear) {
        t0 = schedule_get_ticks();
        kmemset(disp->back_buffer, 0, disp->buffer_size);
        t1 = schedule_get_ticks();
        t_clear += (t1 - t0);
    }
    
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

    t_kernel += schedule_get_ticks() - k0;
    
    // 更新FPS
    disp->frame_count++;
    frame_count++;
    
    // 每 60 帧打印一次
    if (frame_count >= 60) {
        u32 now = schedule_get_ticks();
        u32 dt = now - fps_t0;
        u32 app_fps = dt > 0 ? (frame_count * SCHEDULE_FREQUENCY) / dt : 0;
        u32 k_us = (t_kernel * 1000) / frame_count;
        log_info("Render: app_fps=%d k_us=%d clear=%d composite=%d "
                 "cursor=%d flip=%d (skip=%d)\n",
                 app_fps, k_us, t_clear, t_composite, t_cursor, t_flip,
                 skip_count);
        t_clear = t_composite = t_cursor = t_flip = t_kernel = 0;
        frame_count = 0;
        skip_count = 0;
        fps_t0 = now;
    }
}

void xwin_render_window(xdisplay_t* disp, xwindow_t* win) {
    if (disp == NULL || win == NULL || !win->visible) return;
    
    // 渲染窗口内容到后备缓冲
    xwin_composite_window(disp, win);
}

/* SVC 下 TTBR0=upage；LCD 必须 PAGE_RW_NC。
 * 多进程各自有 upage：不能用全局 fb_mapped_tid 互斥，否则 A/B 每帧互踢重映 150 页。 */
int xwin_map_framebuffer(xdisplay_t* disp) {
    if (disp == NULL || disp->vga == NULL || disp->buffer_size == 0) {
        return -1;
    }

    u32 va = (u32)(uintptr_t)disp->vga->frambuffer;
    u32 pa = (u32)(uintptr_t)disp->vga->pframbuffer;

    /* 防止 VA/PA 颠倒或被堆指针污染 */
    if ((va & 0xff000000u) == 0xfe000000u &&
        (pa & 0xff000000u) == 0xfb000000u) {
        u32 tmp = va;
        va = pa;
        pa = tmp;
        log_warn("xwin: swapped inverted va/pa -> va=%x pa=%x\n", va, pa);
    }
    if ((va & 0xff000000u) != 0xfb000000u ||
        (pa & 0xff000000u) != 0xfe000000u) {
        log_warn("xwin: map force alias (was va=%x pa=%x)\n", va, pa);
        va = 0xfb000000u;
        pa = 0xfe000000u;
    }
    disp->vga->frambuffer = (u32*)(uintptr_t)va;
    disp->vga->pframbuffer = (u32*)(uintptr_t)pa;
    disp->lcd_va = disp->vga->frambuffer;
    disp->lcd_pa = disp->vga->pframbuffer;

    thread_t* cur = thread_current();
    void* kpd = page_kernel_dir();

    if (cur == NULL || cur->vm == NULL || cur->vm->upage == NULL) {
        if (kpd != NULL &&
            page_v2p((u64*)kpd, (void*)(uintptr_t)va) != NULL) {
            return 0;
        }
        log_warn("xwin: map fb no user vm va=%x\n", va);
        return -1;
    }

    /* 以当前进程页表为准：已是 fb→fe 则跳过（多 gui 各自保留映射） */
    {
        void* got = page_v2p((u64*)cur->vm->upage, (void*)(uintptr_t)va);
        if (got != NULL &&
            ((u32)(uintptr_t)got & 0xff000000u) == 0xfe000000u) {
            disp->fb_mapped_tid = cur->id;
            return 0;
        }
    }

    u32 pages = (disp->buffer_size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (u32 i = 0; i < pages; i++) {
        page_map_current(va + i * PAGE_SIZE, pa + i * PAGE_SIZE, PAGE_RW_NC);
        page_map(va + i * PAGE_SIZE, pa + i * PAGE_SIZE, PAGE_RW_NC);
    }

    dsb();
    {
        void* got = page_v2p((u64*)cur->vm->upage, (void*)(uintptr_t)va);
        if (got == NULL ||
            ((u32)(uintptr_t)got & 0xff000000u) != 0xfe000000u) {
            log_error("xwin: fb map failed va=%x pa=%x got=%x tid=%d\n", va,
                      pa, (u32)(uintptr_t)got, cur->id);
            return -1;
        }
    }
    disp->fb_mapped_tid = cur->id;
    log_info("xwin: mapped LCD va=%x -> pa=%x (%d pages tid=%d)\n", va, pa,
             pages, cur->id);
    return 0;
}

void xwin_flip_buffer(xdisplay_t* disp) {
    if (disp == NULL || disp->vga == NULL || disp->back_buffer == NULL) {
        return;
    }

    if (disp->vga->frambuffer != NULL && disp->buffer_size > 0) {
        xwin_map_framebuffer(disp);
        if (disp->back_buffer != (u32*)disp->vga->frambuffer) {
            kmemcpy(disp->vga->frambuffer, disp->back_buffer, disp->buffer_size);
        }
        /* NC 目标：保证写完成后再让 DE 扫 */
        dsb();
    }
    if (disp->vga->flip_buffer != NULL) {
        disp->vga->flip_buffer(disp->vga, 0);
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
    if (dst == NULL || src == dst) {
        win->damaged = 0;
        return;
    }
    
    u32 screen_w = disp->vga->width;
    u32 screen_h = disp->vga->height;
    
    // 窗口外框
    i32 sx = win->abs_x;
    i32 sy = win->abs_y;
    i32 ex = sx + win->width;
    i32 ey = sy + win->height;

    /* 有边框时：客户区画在标题栏/边框内侧，避免被装饰盖住 */
    i32 cox = 0, coy = 0, cbw = 0;
    int bordered = (win->flags & XWIN_FLAG_BORDERED) && win != disp->root_window;
    if (bordered) {
        xtheme_t* t = xtheme_current(disp);
        cbw = (i32)t->border_width;
        coy = (i32)t->title_bar_height;
        cox = cbw;
    }

    i32 csx = sx + cox;
    i32 csy = sy + coy;
    i32 cex = ex - cbw;
    i32 cey = ey - cbw;
    
    // 客户区裁剪到屏幕
    i32 clip_sx = (csx < 0) ? 0 : csx;
    i32 clip_sy = (csy < 0) ? 0 : csy;
    i32 clip_ex = (cex > (i32)screen_w) ? (i32)screen_w : cex;
    i32 clip_ey = (cey > (i32)screen_h) ? (i32)screen_h : cey;
    
    i32 copy_width = clip_ex - clip_sx;
    if (copy_width > 0 && clip_sy < clip_ey) {
        for (i32 y = clip_sy; y < clip_ey; y++) {
            i32 src_y = y - csy;
            i32 src_x = clip_sx - csx;
            if (src_y < 0 || src_y >= (i32)win->height) continue;
            if (src_x < 0) src_x = 0;
            i32 row_w = copy_width;
            if (src_x + row_w > (i32)win->width) {
                row_w = (i32)win->width - src_x;
            }
            if (row_w <= 0) continue;

            u32* dst_row = dst + y * screen_w + clip_sx;
            u32* src_row = src + src_y * win->width + src_x;
            kmemcpy(dst_row, src_row, row_w * sizeof(u32));
        }
    }
    
    // 装饰画在外框上（标题栏/边框），不覆盖客户区
    if (bordered) {
        xtheme_render_decoration(disp, win, dst, screen_w, screen_h, sx, sy, ex, ey);
    }
    
    win->damaged = 0;
}

