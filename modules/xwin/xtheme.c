/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * X Window System - Theme System
 ********************************************************************/
#include "xwin.h"
#include "font.h"

// ========== 默认渲染函数实现 ==========

// 默认绘制边框
static void default_draw_border(xdisplay_t* disp, xwindow_t* win,
                                u32* buffer, u32 buf_w, u32 buf_h,
                                i32 sx, i32 sy, i32 ex, i32 ey) {
    xtheme_t* t = disp->theme;
    u32 border_color = win->focused ? t->border_active : t->border_inactive;
    i32 bw = (i32)t->border_width;
    
    // 裁剪区域
    i32 clip_sx = (sx < 0) ? 0 : sx;
    i32 clip_sy = (sy < 0) ? 0 : sy;
    i32 clip_ex = (ex > (i32)buf_w) ? buf_w : ex;
    i32 clip_ey = (ey > (i32)buf_h) ? buf_h : ey;
    i32 copy_width = clip_ex - clip_sx;
    
    // 顶部边框
    for (i32 y = clip_sy; y < clip_sy + bw && y < clip_ey; y++) {
        u32* row = buffer + y * buf_w + clip_sx;
        for (i32 x = 0; x < copy_width; x++) {
            row[x] = border_color;
        }
    }
    
    // 左右边框
    for (i32 y = clip_sy + bw; y < clip_ey - bw; y++) {
        u32* row = buffer + y * buf_w;
        for (i32 x = clip_sx; x < clip_sx + bw && x < clip_ex; x++) {
            row[x] = border_color;
        }
        for (i32 x = clip_ex - bw; x < clip_ex; x++) {
            row[x] = border_color;
        }
    }
    
    // 底部边框
    for (i32 y = clip_ey - bw; y < clip_ey; y++) {
        u32* row = buffer + y * buf_w + clip_sx;
        for (i32 x = 0; x < copy_width; x++) {
            row[x] = border_color;
        }
    }
}

// 默认绘制标题栏背景
static void default_draw_titlebar(xdisplay_t* disp, xwindow_t* win,
                                  u32* buffer, u32 buf_w, u32 buf_h,
                                  i32 sx, i32 sy, i32 ex, i32 ey) {
    xtheme_t* t = disp->theme;
    u32 title_h = t->title_bar_height;
    
    u32 bg_top = win->focused ? t->titlebar_bg_top : t->titlebar_inactive;
    u32 bg_bottom = win->focused ? t->titlebar_bg_bottom : t->titlebar_inactive;
    
    i32 clip_sx = (sx < 0) ? 0 : sx;
    i32 clip_sy = (sy < 0) ? 0 : sy;
    i32 clip_ex = (ex > (i32)buf_w) ? buf_w : ex;
    i32 clip_ey = (ey > (i32)buf_h) ? buf_h : ey;
    
    i32 title_end = (sy + (i32)title_h < clip_ey) ? sy + (i32)title_h : clip_ey;
    i32 copy_width = clip_ex - clip_sx;
    
    for (i32 y = clip_sy; y < title_end; y++) {
        u32* row = buffer + y * buf_w + clip_sx;
        u32 ratio = (y - sy) * 256 / title_h;
        u32 r = ((bg_top >> 16 & 0xFF) * (256 - ratio) + (bg_bottom >> 16 & 0xFF) * ratio) >> 8;
        u32 g = ((bg_top >> 8 & 0xFF) * (256 - ratio) + (bg_bottom >> 8 & 0xFF) * ratio) >> 8;
        u32 b = ((bg_top & 0xFF) * (256 - ratio) + (bg_bottom & 0xFF) * ratio) >> 8;
        u32 color = 0xFF000000 | (r << 16) | (g << 8) | b;
        for (i32 x = 0; x < copy_width; x++) {
            row[x] = color;
        }
    }
}

// 默认绘制标题文字
static void default_draw_title_text(xdisplay_t* disp, xwindow_t* win,
                                    u32* buffer, u32 buf_w, u32 buf_h,
                                    i32 sx, i32 sy, i32 ex, i32 ey) {
    xtheme_t* t = disp->theme;
    
    if (win->title[0] == '\0') return;
    
    u32 text_color = win->focused ? t->title_text_color : t->title_text_inactive;
    u32 title_h = t->title_bar_height;
    u32 btn_area = t->btn_size * 3 + t->btn_gap * 4;
    
    i32 text_x = sx + 12;
    i32 text_y = sy + (title_h - 16) / 2;
    
    i32 clip_sx = (sx < 0) ? 0 : sx;
    i32 clip_sy = (sy < 0) ? 0 : sy;
    i32 clip_ex = (ex > (i32)buf_w) ? buf_w : ex;
    i32 clip_ey = (ey > (i32)buf_h) ? buf_h : ey;
    
    const char* p = win->title;
    while (*p && text_x < ex - (i32)btn_area) {
        if (*p > 0 && *p < 128) {
            const u8* glyph = font8x16[(u32)*p];
            for (u32 row = 0; row < 16; row++) {
                u8 bits = glyph[row];
                for (u32 col = 0; col < 8; col++) {
                    if (bits & (0x80 >> col)) {
                        i32 px = text_x + col;
                        i32 py = text_y + (i32)row;
                        if (px >= clip_sx && px < clip_ex && py >= clip_sy && py < clip_ey) {
                            buffer[py * buf_w + px] = text_color;
                        }
                    }
                }
            }
        }
        text_x += 8;
        p++;
    }
}

// 默认绘制按钮
static void default_draw_buttons(xdisplay_t* disp, xwindow_t* win,
                                 u32* buffer, u32 buf_w, u32 buf_h,
                                 i32 sx, i32 sy, i32 ex, i32 ey) {
    xtheme_t* t = disp->theme;
    
    u32 title_h = t->title_bar_height;
    u32 btn_size = t->btn_size;
    u32 btn_gap = t->btn_gap;
    i32 btn_y = sy + (title_h - btn_size) / 2;
    i32 btn_x = ex - btn_size - btn_gap;
    i32 btn_radius = btn_size / 2;
    
    i32 clip_sx = (sx < 0) ? 0 : sx;
    i32 clip_sy = (sy < 0) ? 0 : sy;
    i32 clip_ex = (ex > (i32)buf_w) ? buf_w : ex;
    i32 clip_ey = (ey > (i32)buf_h) ? buf_h : ey;
    
    if (t->button_style == XBUTTON_STYLE_CIRCLE) {
        // 关闭按钮
        for (i32 y = 0; y < (i32)btn_size; y++) {
            for (i32 x = 0; x < (i32)btn_size; x++) {
                i32 px = btn_x + x;
                i32 py = btn_y + y;
                if (px >= clip_sx && px < clip_ex && py >= clip_sy && py < clip_ey) {
                    i32 dx = x - btn_radius;
                    i32 dy = y - btn_radius;
                    if (dx*dx + dy*dy <= btn_radius * btn_radius) {
                        buffer[py * buf_w + px] = t->btn_close;
                    }
                }
            }
        }
        // X 图标
        for (i32 i = 3; i < (i32)btn_size - 3; i++) {
            i32 px1 = btn_x + i, py1 = btn_y + i;
            i32 px2 = btn_x + btn_size - 1 - i, py2 = btn_y + i;
            if (px1 >= clip_sx && px1 < clip_ex && py1 >= clip_sy && py1 < clip_ey) {
                buffer[py1 * buf_w + px1] = t->btn_icon;
            }
            if (px2 >= clip_sx && px2 < clip_ex && py2 >= clip_sy && py2 < clip_ey) {
                buffer[py2 * buf_w + px2] = t->btn_icon;
            }
        }
        
        // 最大化按钮
        btn_x -= btn_size + btn_gap;
        for (i32 y = 0; y < (i32)btn_size; y++) {
            for (i32 x = 0; x < (i32)btn_size; x++) {
                i32 px = btn_x + x;
                i32 py = btn_y + y;
                if (px >= clip_sx && px < clip_ex && py >= clip_sy && py < clip_ey) {
                    i32 dx = x - btn_radius;
                    i32 dy = y - btn_radius;
                    if (dx*dx + dy*dy <= btn_radius * btn_radius) {
                        buffer[py * buf_w + px] = t->btn_maximize;
                    }
                }
            }
        }
        // 方框图标
        for (i32 y = 3; y < (i32)btn_size - 3; y++) {
            for (i32 x = 3; x < (i32)btn_size - 3; x++) {
                if (y == 3 || y == (i32)btn_size - 4 || x == 3 || x == (i32)btn_size - 4) {
                    i32 px = btn_x + x;
                    i32 py = btn_y + y;
                    if (px >= clip_sx && px < clip_ex && py >= clip_sy && py < clip_ey) {
                        buffer[py * buf_w + px] = t->btn_icon;
                    }
                }
            }
        }
        
        // 最小化按钮
        btn_x -= btn_size + btn_gap;
        for (i32 y = 0; y < (i32)btn_size; y++) {
            for (i32 x = 0; x < (i32)btn_size; x++) {
                i32 px = btn_x + x;
                i32 py = btn_y + y;
                if (px >= clip_sx && px < clip_ex && py >= clip_sy && py < clip_ey) {
                    i32 dx = x - btn_radius;
                    i32 dy = y - btn_radius;
                    if (dx*dx + dy*dy <= btn_radius * btn_radius) {
                        buffer[py * buf_w + px] = t->btn_minimize;
                    }
                }
            }
        }
        // 横线图标
        for (i32 x = 3; x < (i32)btn_size - 3; x++) {
            i32 px = btn_x + x;
            i32 py = btn_y + btn_radius;
            if (px >= clip_sx && px < clip_ex && py >= clip_sy && py < clip_ey) {
                buffer[py * buf_w + px] = t->btn_icon;
            }
        }
    } else {
        // 方形按钮
        // 关闭按钮
        for (i32 y = 0; y < (i32)btn_size; y++) {
            for (i32 x = 0; x < (i32)btn_size; x++) {
                i32 px = btn_x + x;
                i32 py = btn_y + y;
                if (px >= clip_sx && px < clip_ex && py >= clip_sy && py < clip_ey) {
                    buffer[py * buf_w + px] = t->btn_close;
                }
            }
        }
        // X 图标
        for (i32 i = 2; i < (i32)btn_size - 2; i++) {
            i32 px1 = btn_x + i, py1 = btn_y + i;
            i32 px2 = btn_x + btn_size - 1 - i, py2 = btn_y + i;
            if (px1 >= clip_sx && px1 < clip_ex && py1 >= clip_sy && py1 < clip_ey) {
                buffer[py1 * buf_w + px1] = t->btn_icon;
            }
            if (px2 >= clip_sx && px2 < clip_ex && py2 >= clip_sy && py2 < clip_ey) {
                buffer[py2 * buf_w + px2] = t->btn_icon;
            }
        }
        
        // 最大化按钮
        btn_x -= btn_size + btn_gap;
        for (i32 y = 0; y < (i32)btn_size; y++) {
            for (i32 x = 0; x < (i32)btn_size; x++) {
                i32 px = btn_x + x;
                i32 py = btn_y + y;
                if (px >= clip_sx && px < clip_ex && py >= clip_sy && py < clip_ey) {
                    buffer[py * buf_w + px] = t->btn_maximize;
                }
            }
        }
        // 方框图标
        for (i32 y = 2; y < (i32)btn_size - 2; y++) {
            for (i32 x = 2; x < (i32)btn_size - 2; x++) {
                if (y == 2 || y == (i32)btn_size - 3 || x == 2 || x == (i32)btn_size - 3) {
                    i32 px = btn_x + x;
                    i32 py = btn_y + y;
                    if (px >= clip_sx && px < clip_ex && py >= clip_sy && py < clip_ey) {
                        buffer[py * buf_w + px] = t->btn_icon;
                    }
                }
            }
        }
        
        // 最小化按钮
        btn_x -= btn_size + btn_gap;
        for (i32 y = 0; y < (i32)btn_size; y++) {
            for (i32 x = 0; x < (i32)btn_size; x++) {
                i32 px = btn_x + x;
                i32 py = btn_y + y;
                if (px >= clip_sx && px < clip_ex && py >= clip_sy && py < clip_ey) {
                    buffer[py * buf_w + px] = t->btn_minimize;
                }
            }
        }
        // 横线图标
        for (i32 x = 2; x < (i32)btn_size - 2; x++) {
            i32 px = btn_x + x;
            i32 py = btn_y + btn_size - 4;
            if (px >= clip_sx && px < clip_ex && py >= clip_sy && py < clip_ey) {
                buffer[py * buf_w + px] = t->btn_icon;
            }
        }
    }
}

// 默认绘制整个装饰
static void default_draw_decoration(xdisplay_t* disp, xwindow_t* win,
                                    u32* buffer, u32 buf_w, u32 buf_h,
                                    i32 sx, i32 sy, i32 ex, i32 ey) {
    xtheme_t* t = disp->theme;
    
    // 绘制边框
    if (t->draw_border) {
        t->draw_border(disp, win, buffer, buf_w, buf_h, sx, sy, ex, ey);
    } else {
        default_draw_border(disp, win, buffer, buf_w, buf_h, sx, sy, ex, ey);
    }
    
    // 绘制标题栏
    if (t->draw_titlebar) {
        t->draw_titlebar(disp, win, buffer, buf_w, buf_h, sx, sy, ex, ey);
    } else {
        default_draw_titlebar(disp, win, buffer, buf_w, buf_h, sx, sy, ex, ey);
    }
    
    // 绘制标题文字
    if (t->draw_title_text) {
        t->draw_title_text(disp, win, buffer, buf_w, buf_h, sx, sy, ex, ey);
    } else {
        default_draw_title_text(disp, win, buffer, buf_w, buf_h, sx, sy, ex, ey);
    }
    
    // 绘制按钮
    if (t->draw_buttons) {
        t->draw_buttons(disp, win, buffer, buf_w, buf_h, sx, sy, ex, ey);
    } else {
        default_draw_buttons(disp, win, buffer, buf_w, buf_h, sx, sy, ex, ey);
    }
}

// 默认绘制桌面
static void default_draw_desktop(xdisplay_t* disp, xwindow_t* desktop,
                                  u32* buffer, u32 buf_w, u32 buf_h) {
    xtheme_t* t = disp->theme;
    
    u32 bg_top = t->desktop_bg_top;
    u32 bg_bottom = t->desktop_bg_bottom;
    
    for (u32 y = 0; y < buf_h; y++) {
        u32 ratio = y * 256 / buf_h;
        u32 r = ((bg_top >> 16 & 0xFF) * (256 - ratio) + (bg_bottom >> 16 & 0xFF) * ratio) >> 8;
        u32 g = ((bg_top >> 8 & 0xFF) * (256 - ratio) + (bg_bottom >> 8 & 0xFF) * ratio) >> 8;
        u32 b = ((bg_top & 0xFF) * (256 - ratio) + (bg_bottom & 0xFF) * ratio) >> 8;
        u32 color = 0xFF000000 | (r << 16) | (g << 8) | b;
        
        u32* row = buffer + y * buf_w;
        for (u32 x = 0; x < buf_w; x++) {
            row[x] = color;
        }
    }
}

// ========== 预设主题定义 ==========

// 深色主题（默认）
static xtheme_t theme_dark = {
    .name = "Dark",
    .title_bar_height = 28,
    .border_width = 1,
    .corner_radius = 8,
    
    .titlebar_bg_top    = 0xFF3A3A3A,
    .titlebar_bg_bottom = 0xFF2D2D2D,
    .titlebar_inactive  = 0xFF1E1E1E,
    .title_text_color   = 0xFFE0E0E0,
    .title_text_inactive = 0xFF808080,
    
    .border_active   = 0xFF0078D4,
    .border_inactive = 0xFF404040,
    
    .btn_close    = 0xFFC42B1C,
    .btn_close_hover = 0xFFE81123,
    .btn_minimize = 0xFF3A3A3A,
    .btn_maximize = 0xFF3A3A3A,
    .btn_icon     = 0xFFFFFFFF,
    .btn_size     = 12,
    .btn_gap      = 8,
    
    .button_style = XBUTTON_STYLE_CIRCLE,
    .border_style = XBORDER_STYLE_THIN,
    
    .desktop_bg_top    = 0xFF1A1A2E,
    .desktop_bg_bottom = 0xFF16213E,
    .desktop_solid     = 0xFF1E1E1E,
    
    .shadow_color = 0x80000000,
    .shadow_size  = 8,
    
    // 使用默认渲染函数
    .draw_decoration = default_draw_decoration,
    .draw_border = NULL,
    .draw_titlebar = NULL,
    .draw_title_text = NULL,
    .draw_buttons = NULL,
    .draw_desktop = default_draw_desktop,
    .draw_shadow = NULL,
    .user_data = NULL,
};

// 浅色主题
static xtheme_t theme_light = {
    .name = "Light",
    .title_bar_height = 28,
    .border_width = 1,
    .corner_radius = 8,
    
    .titlebar_bg_top    = 0xFFF0F0F0,
    .titlebar_bg_bottom = 0xFFE0E0E0,
    .titlebar_inactive  = 0xFFF5F5F5,
    .title_text_color   = 0xFF1A1A1A,
    .title_text_inactive = 0xFF808080,
    
    .border_active   = 0xFF0078D4,
    .border_inactive = 0xFFD0D0D0,
    
    .btn_close    = 0xFFC42B1C,
    .btn_close_hover = 0xFFE81123,
    .btn_minimize = 0xFFE0E0E0,
    .btn_maximize = 0xFFE0E0E0,
    .btn_icon     = 0xFF404040,
    .btn_size     = 12,
    .btn_gap      = 8,
    
    .button_style = XBUTTON_STYLE_CIRCLE,
    .border_style = XBORDER_STYLE_THIN,
    
    .desktop_bg_top    = 0xFFE8E8E8,
    .desktop_bg_bottom = 0xFFD0D0D0,
    .desktop_solid     = 0xFFF0F0F0,
    
    .shadow_color = 0x40000000,
    .shadow_size  = 6,
    
    .draw_decoration = default_draw_decoration,
    .draw_border = NULL,
    .draw_titlebar = NULL,
    .draw_title_text = NULL,
    .draw_buttons = NULL,
    .draw_desktop = default_draw_desktop,
    .draw_shadow = NULL,
    .user_data = NULL,
};

// 蓝色主题
static xtheme_t theme_blue = {
    .name = "Blue",
    .title_bar_height = 32,
    .border_width = 2,
    .corner_radius = 6,
    
    .titlebar_bg_top    = 0xFF0078D4,
    .titlebar_bg_bottom = 0xFF005A9E,
    .titlebar_inactive  = 0xFF1E3A5F,
    .title_text_color   = 0xFFFFFFFF,
    .title_text_inactive = 0xFFA0C0E0,
    
    .border_active   = 0xFF0078D4,
    .border_inactive = 0xFF304050,
    
    .btn_close    = 0xFFC42B1C,
    .btn_close_hover = 0xFFE81123,
    .btn_minimize = 0xFF2080C0,
    .btn_maximize = 0xFF2080C0,
    .btn_icon     = 0xFFFFFFFF,
    .btn_size     = 14,
    .btn_gap      = 6,
    
    .button_style = XBUTTON_STYLE_SQUARE,
    .border_style = XBORDER_STYLE_THICK,
    
    .desktop_bg_top    = 0xFF001E3C,
    .desktop_bg_bottom = 0xFF000A18,
    .desktop_solid     = 0xFF001E3C,
    
    .shadow_color = 0x60000000,
    .shadow_size  = 10,
    
    .draw_decoration = default_draw_decoration,
    .draw_border = NULL,
    .draw_titlebar = NULL,
    .draw_title_text = NULL,
    .draw_buttons = NULL,
    .draw_desktop = default_draw_desktop,
    .draw_shadow = NULL,
    .user_data = NULL,
};

// 经典主题
static xtheme_t theme_classic = {
    .name = "Classic",
    .title_bar_height = 20,
    .border_width = 2,
    .corner_radius = 0,
    
    .titlebar_bg_top    = 0xFF000080,
    .titlebar_bg_bottom = 0xFF000080,
    .titlebar_inactive  = 0xFF808080,
    .title_text_color   = 0xFFFFFFFF,
    .title_text_inactive = 0xFFC0C0C0,
    
    .border_active   = 0xFF808080,
    .border_inactive = 0xFF808080,
    
    .btn_close    = 0xFFC0C0C0,
    .btn_close_hover = 0xFFC0C0C0,
    .btn_minimize = 0xFFC0C0C0,
    .btn_maximize = 0xFFC0C0C0,
    .btn_icon     = 0xFF000000,
    .btn_size     = 12,
    .btn_gap      = 2,
    
    .button_style = XBUTTON_STYLE_SQUARE,
    .border_style = XBORDER_STYLE_THICK,
    
    .desktop_bg_top    = 0xFF008080,
    .desktop_bg_bottom = 0xFF008080,
    .desktop_solid     = 0xFF008080,
    
    .shadow_color = 0x00000000,
    .shadow_size  = 0,
    
    .draw_decoration = default_draw_decoration,
    .draw_border = NULL,
    .draw_titlebar = NULL,
    .draw_title_text = NULL,
    .draw_buttons = NULL,
    .draw_desktop = default_draw_desktop,
    .draw_shadow = NULL,
    .user_data = NULL,
};

// 主题数组
static xtheme_t* themes[XTHEME_COUNT] = {
    &theme_dark,
    &theme_light,
    &theme_blue,
    &theme_classic
};

// ========== 主题 API 实现 ==========

xtheme_t* xtheme_get(xtheme_id_t id) {
    if (id >= XTHEME_COUNT) id = XTHEME_DARK;
    return themes[id];
}

void xtheme_set(xdisplay_t* disp, xtheme_id_t id) {
    if (disp == NULL) return;
    if (id >= XTHEME_COUNT) id = XTHEME_DARK;
    
    disp->theme_id = id;
    disp->theme = themes[id];
}

xtheme_t* xtheme_current(xdisplay_t* disp) {
    if (disp == NULL || disp->theme == NULL) {
        return &theme_dark;
    }
    return disp->theme;
}

void xtheme_apply(xdisplay_t* disp) {
    if (disp == NULL) return;
    
    // 重绘桌面
    if (disp->root_window != NULL) {
        xwin_damage_all(disp->root_window);
    }
    
    // 重绘所有窗口
    for (u32 i = 0; i < disp->window_count; i++) {
        xwindow_t* win = disp->windows[i];
        if (win != NULL) {
            xwin_damage_all(win);
        }
    }
}

// ========== 主题渲染入口函数 ==========

void xtheme_render_decoration(xdisplay_t* disp, xwindow_t* win,
                               u32* buffer, u32 buf_w, u32 buf_h,
                               i32 sx, i32 sy, i32 ex, i32 ey) {
    xtheme_t* t = xtheme_current(disp);
    
    if (t->draw_decoration) {
        t->draw_decoration(disp, win, buffer, buf_w, buf_h, sx, sy, ex, ey);
    } else {
        default_draw_decoration(disp, win, buffer, buf_w, buf_h, sx, sy, ex, ey);
    }
}

void xtheme_render_desktop(xdisplay_t* disp, xwindow_t* desktop,
                           u32* buffer, u32 buf_w, u32 buf_h) {
    xtheme_t* t = xtheme_current(disp);
    
    if (t->draw_desktop) {
        t->draw_desktop(disp, desktop, buffer, buf_w, buf_h);
    } else {
        default_draw_desktop(disp, desktop, buffer, buf_w, buf_h);
    }
}
