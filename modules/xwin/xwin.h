/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * X Window System for Duck Kernel
 ********************************************************************/
#ifndef XWIN_H
#define XWIN_H

#include "kernel/kernel.h"
#include "modules/vga/vga.h"
#include "algorithm/queue_pool.h"

// ========== 颜色定义 ==========
#define XCOLOR_BLACK       0x00000000
#define XCOLOR_WHITE       0x00FFFFFF
#define XCOLOR_RED         0x00FF0000
#define XCOLOR_GREEN       0x0000FF00
#define XCOLOR_BLUE        0x000000FF
#define XCOLOR_YELLOW      0x00FFFF00
#define XCOLOR_CYAN        0x0000FFFF
#define XCOLOR_MAGENTA     0x00FF00FF
#define XCOLOR_GRAY        0x00808080
#define XCOLOR_LIGHT_GRAY  0x00C0C0C0
#define XCOLOR_DARK_GRAY   0x00404040

// ========== 窗口标志 ==========
#define XWIN_FLAG_VISIBLE    0x01
#define XWIN_FLAG_FOCUSABLE  0x02
#define XWIN_FLAG_BORDERED   0x04
#define XWIN_FLAG_DRAGGABLE  0x08
#define XWIN_FLAG_RESIZABLE  0x10
#define XWIN_FLAG_ROOT       0x80

// ========== 事件类型 ==========
enum {
    XEVENT_NONE = 0,
    XEVENT_MOUSE_MOVE,
    XEVENT_MOUSE_DOWN,
    XEVENT_MOUSE_UP,
    XEVENT_MOUSE_CLICK,
    XEVENT_MOUSE_DBLCLICK,
    XEVENT_MOUSE_WHEEL,
    XEVENT_KEY_DOWN,
    XEVENT_KEY_UP,
    XEVENT_KEY_PRESS,
    XEVENT_EXPOSE,       // 重绘请求
    XEVENT_RESIZE,
    XEVENT_MOVE,
    XEVENT_CLOSE,
    XEVENT_FOCUS_IN,
    XEVENT_FOCUS_OUT,
    XEVENT_CREATE,
    XEVENT_DESTROY,
};

// ========== 鼠标按键 ==========
enum {
    XBUTTON_LEFT = 1,
    XBUTTON_RIGHT = 2,
    XBUTTON_MIDDLE = 4,
};

// ========== 窗口结构 ==========
typedef struct xwindow xwindow_t;

typedef struct xwindow {
    u32 id;                    // 窗口ID
    char title[64];            // 标题
    i32 x, y;                  // 位置 (相对于父窗口)
    i32 abs_x, abs_y;          // 绝对位置 (屏幕坐标)
    u32 width, height;         // 尺寸
    u32 min_width, min_height; // 最小尺寸
    u32 max_width, max_height; // 最大尺寸
    u32* framebuffer;          // 窗口缓冲区 (ARGB)
    u32 fb_size;               // 缓冲区大小
    u32 zorder;                // Z序
    u32 flags;                 // 标志
    u32 visible;               // 可见性
    u32 focused;               // 焦点
    u32 damaged;               // 损坏区域标记
    u32 bg_color;              // 背景色
    
    // 窗口树结构
    xwindow_t* parent;
    xwindow_t* first_child;
    xwindow_t* last_child;
    xwindow_t* prev_sibling;
    xwindow_t* next_sibling;
    
    // 回调函数
    void (*on_event)(xwindow_t* win, void* event);
    void* user_data;           // 用户数据
    
} xwindow_t;

// ========== 图形上下文 ==========
typedef struct xgc {
    u32 foreground;            // 前景色
    u32 background;            // 背景色
    u32 line_width;            // 线宽
    u32 line_style;            // 线型
    u32 font_size;             // 字体大小
    i32 clip_x1, clip_y1;      // 裁剪区域
    i32 clip_x2, clip_y2;
    u32 clip_enabled;          // 裁剪使能
} xgc_t;

// ========== 事件结构 ==========
typedef struct xevent {
    u32 type;                  // 事件类型
    u32 window_id;             // 目标窗口ID
    u32 time;                  // 时间戳
    union {
        struct { 
            i32 x, y;          // 鼠标位置
            i32 rel_x, rel_y;  // 相对移动
            u32 button;        // 按键状态
            i32 wheel;         // 滚轮
        } mouse;
        struct { 
            u32 keycode;       // 键码
            u32 scancode;      // 扫描码
            u32 mods;          // 修饰键 (Shift/Ctrl/Alt)
            u32 pressed;       // 按下/释放
            char ch;           // 字符 (如果有)
        } key;
        struct { 
            u32 width, height; 
            u32 old_width, old_height;
        } resize;
        struct { 
            i32 x, y;
            i32 old_x, old_y;
        } move;
    } data;
} xevent_t;

// ========== 显示服务器结构 ==========
typedef struct xdisplay {
    vga_device_t* vga;         // 底层显示设备
    u32* screen_buffer;        // 屏幕缓冲
    u32* back_buffer;          // 后备缓冲 (双缓冲)
    u32 buffer_size;           // 缓冲区大小
    
    xwindow_t* root_window;    // 根窗口 (桌面)
    xwindow_t* focused_window; // 焦点窗口
    xwindow_t* grab_window;    // 抓取窗口 (拖拽/菜单)
    xwindow_t** windows;       // 窗口数组 (按zorder)
    u32 window_count;          // 窗口数量
    u32 window_capacity;       // 窗口数组容量
    u32 next_window_id;        // 下一个窗口ID
    
    ring_queue_t* event_queue;  // 事件队列
    
    // 鼠标状态
    i32 mouse_x, mouse_y;      // 鼠标位置
    u32 mouse_buttons;         // 按键状态
    u32 mouse_visible;         // 鼠标可见性
    u32 mouse_cursor;          // 光标类型
    
    // 键盘状态
    u32 key_mods;              // 修饰键状态
    
    // 拖拽状态
    xwindow_t* drag_window;    // 正在拖拽的窗口
    i32 drag_offset_x, drag_offset_y;
    u32 drag_button;
    
    // 渲染统计
    u32 fps;                   // 帧率
    u32 frame_count;           // 帧计数
    u32 last_fps_time;         // 上次FPS计算时间
    
} xdisplay_t;

// ========== 显示设备 ==========
typedef struct xwin_device {
    xdisplay_t* display;
} xwin_device_t;

// ========== 窗口管理 API ==========

// 初始化
int xwin_init(xdisplay_t* disp, vga_device_t* vga);
void xwin_exit(xdisplay_t* disp);

// 窗口创建/销毁
xwindow_t* xwin_create_window(xdisplay_t* disp, 
                              xwindow_t* parent,
                              i32 x, i32 y, 
                              u32 width, u32 height,
                              u32 flags);
void xwin_destroy_window(xdisplay_t* disp, xwindow_t* win);
xwindow_t* xwin_find_window(xdisplay_t* disp, u32 id);
xwindow_t* xwin_find_window_at(xdisplay_t* disp, i32 x, i32 y);

// 窗口属性
void xwin_set_title(xwindow_t* win, const char* title);
void xwin_move(xdisplay_t* disp, xwindow_t* win, i32 x, i32 y);
void xwin_resize(xdisplay_t* disp, xwindow_t* win, u32 w, u32 h);
void xwin_set_zorder(xdisplay_t* disp, xwindow_t* win, u32 zorder);
void xwin_raise(xdisplay_t* disp, xwindow_t* win);
void xwin_lower(xdisplay_t* disp, xwindow_t* win);
void xwin_show(xdisplay_t* disp, xwindow_t* win, int show);
void xwin_set_bg_color(xwindow_t* win, u32 color);

// 焦点管理
void xwin_set_focus(xdisplay_t* disp, xwindow_t* win);
xwindow_t* xwin_get_focused(xdisplay_t* disp);

// ========== 图形绘制 API ==========

// 绘制上下文
void xgc_init(xgc_t* gc);
void xgc_set_foreground(xgc_t* gc, u32 color);
void xgc_set_background(xgc_t* gc, u32 color);
void xgc_set_line_width(xgc_t* gc, u32 width);
void xgc_set_clip(xgc_t* gc, i32 x1, i32 y1, i32 x2, i32 y2);
void xgc_clear_clip(xgc_t* gc);

// 基本绘制
void xwin_draw_pixel(xwindow_t* win, i32 x, i32 y, u32 color);
u32 xwin_get_pixel(xwindow_t* win, i32 x, i32 y);
void xwin_draw_line(xwindow_t* win, i32 x1, i32 y1, i32 x2, i32 y2, u32 color);
void xwin_draw_rect(xwindow_t* win, i32 x, i32 y, u32 w, u32 h, u32 color);
void xwin_fill_rect(xwindow_t* win, i32 x, i32 y, u32 w, u32 h, u32 color);
void xwin_draw_circle(xwindow_t* win, i32 cx, i32 cy, u32 r, u32 color);
void xwin_fill_circle(xwindow_t* win, i32 cx, i32 cy, u32 r, u32 color);

// 高级绘制
void xwin_draw_ellipse(xwindow_t* win, i32 x, i32 y, u32 w, u32 h, u32 color);
void xwin_fill_ellipse(xwindow_t* win, i32 x, i32 y, u32 w, u32 h, u32 color);
void xwin_draw_round_rect(xwindow_t* win, i32 x, i32 y, u32 w, u32 h, u32 r, u32 color);
void xwin_fill_round_rect(xwindow_t* win, i32 x, i32 y, u32 w, u32 h, u32 r, u32 color);

// 文本绘制
void xwin_draw_char(xwindow_t* win, i32 x, i32 y, char ch, u32 color, u32 size);
void xwin_draw_text(xwindow_t* win, i32 x, i32 y, const char* text, u32 color);
void xwin_draw_text_size(xwindow_t* win, i32 x, i32 y, const char* text, u32 color, u32 size);
u32 xwin_text_width(const char* text, u32 size);
u32 xwin_text_height(u32 size);

// 图像操作
void xwin_blit(xwindow_t* win, i32 x, i32 y, const u32* data, u32 w, u32 h);
void xwin_blit_transparent(xwindow_t* win, i32 x, i32 y, const u32* data, u32 w, u32 h, u32 key);
void xwin_copy_area(xwindow_t* win, i32 sx, i32 sy, u32 w, u32 h, i32 dx, i32 dy);

// 清除
void xwin_clear(xwindow_t* win);
void xwin_clear_color(xwindow_t* win, u32 color);

// ========== 事件 API ==========

// 事件处理
void xwin_process_events(xdisplay_t* disp);
int xwin_next_event(xdisplay_t* disp, xevent_t* event);
void xwin_send_event(xdisplay_t* disp, xwindow_t* win, xevent_t* event);
void xwin_post_event(xdisplay_t* disp, xevent_t* event);
void xwin_dispatch_event(xdisplay_t* disp, xevent_t* event);

// 输入事件
void xwin_mouse_move(xdisplay_t* disp, i32 x, i32 y);
void xwin_mouse_button(xdisplay_t* disp, u32 button, u32 pressed);
void xwin_mouse_wheel(xdisplay_t* disp, i32 delta);
void xwin_keyboard_event(xdisplay_t* disp, u32 keycode, u32 pressed, u32 mods);

// 损坏区域
void xwin_damage(xwindow_t* win, i32 x, i32 y, u32 w, u32 h);
void xwin_damage_all(xwindow_t* win);

// ========== 渲染 API ==========

// 渲染
void xwin_render(xdisplay_t* disp);
void xwin_render_window(xdisplay_t* disp, xwindow_t* win);
void xwin_flip_buffer(xdisplay_t* disp);
void xwin_update_mouse_cursor(xdisplay_t* disp);

// 窗口合成
void xwin_composite(xdisplay_t* disp);
void xwin_composite_window(xdisplay_t* disp, xwindow_t* win);

// ========== 辅助函数 ==========

// 坐标转换
void xwin_screen_to_window(xwindow_t* win, i32* x, i32* y);
void xwin_window_to_screen(xwindow_t* win, i32* x, i32* y);

// 碰撞检测
int xwin_contains_point(xwindow_t* win, i32 x, i32 y);
int xwin_intersects(xwindow_t* a, xwindow_t* b);

// 边界
void xwin_get_screen_rect(xwindow_t* win, i32* x, i32* y, u32* w, u32* h);

#endif
