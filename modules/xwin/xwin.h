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

// ========== 主题系统 ==========

// 前向声明
struct xdisplay;
struct xwindow;

// 主题ID
typedef enum {
    XTHEME_DARK = 0,      // 深色主题（默认）
    XTHEME_LIGHT,         // 浅色主题
    XTHEME_BLUE,          // 蓝色主题
    XTHEME_CLASSIC,       // 经典主题
    XTHEME_COUNT          // 主题数量
} xtheme_id_t;

// 按钮样式
typedef enum {
    XBUTTON_STYLE_CIRCLE = 0,   // 圆形按钮（现代）
    XBUTTON_STYLE_SQUARE,       // 方形按钮（经典）
    XBUTTON_STYLE_FLAT          // 扁平按钮
} xbutton_style_t;

// 边框样式
typedef enum {
    XBORDER_STYLE_THIN = 0,     // 细边框（现代）
    XBORDER_STYLE_THICK,        // 粗边框（经典）
    XBORDER_STYLE_NONE          // 无边框
} xborder_style_t;

// ========== 主题渲染回调函数类型 ==========

// 绘制窗口边框（在屏幕缓冲区上绘制）
typedef void (*xtheme_draw_border_fn)(
    struct xdisplay* disp,
    struct xwindow* win,
    u32* buffer, u32 buf_w, u32 buf_h,
    i32 sx, i32 sy, i32 ex, i32 ey
);

// 绘制标题栏背景
typedef void (*xtheme_draw_titlebar_fn)(
    struct xdisplay* disp,
    struct xwindow* win,
    u32* buffer, u32 buf_w, u32 buf_h,
    i32 sx, i32 sy, i32 ex, i32 ey
);

// 绘制标题文字
typedef void (*xtheme_draw_title_text_fn)(
    struct xdisplay* disp,
    struct xwindow* win,
    u32* buffer, u32 buf_w, u32 buf_h,
    i32 sx, i32 sy, i32 ex, i32 ey
);

// 绘制窗口按钮
typedef void (*xtheme_draw_buttons_fn)(
    struct xdisplay* disp,
    struct xwindow* win,
    u32* buffer, u32 buf_w, u32 buf_h,
    i32 sx, i32 sy, i32 ex, i32 ey
);

// 绘制整个窗口装饰（边框+标题栏+按钮）
typedef void (*xtheme_draw_decoration_fn)(
    struct xdisplay* disp,
    struct xwindow* win,
    u32* buffer, u32 buf_w, u32 buf_h,
    i32 sx, i32 sy, i32 ex, i32 ey
);

// 绘制桌面背景
typedef void (*xtheme_draw_desktop_fn)(
    struct xdisplay* disp,
    struct xwindow* desktop,
    u32* buffer, u32 buf_w, u32 buf_h
);

// 绘制窗口阴影
typedef void (*xtheme_draw_shadow_fn)(
    struct xdisplay* disp,
    struct xwindow* win,
    u32* buffer, u32 buf_w, u32 buf_h,
    i32 sx, i32 sy, i32 ex, i32 ey
);

// ========== 主题结构 ==========
typedef struct xtheme {
    const char* name;           // 主题名称
    
    // ========== 尺寸参数 ==========
    u32 title_bar_height;       // 标题栏高度
    u32 border_width;           // 边框宽度
    u32 corner_radius;          // 圆角半径
    
    // ========== 颜色参数 ==========
    // 标题栏颜色
    u32 titlebar_bg_top;        // 活动标题栏渐变顶部
    u32 titlebar_bg_bottom;     // 活动标题栏渐变底部
    u32 titlebar_inactive;      // 非活动标题栏
    u32 title_text_color;       // 标题文字颜色
    u32 title_text_inactive;    // 非活动标题文字
    
    // 边框颜色
    u32 border_active;          // 活动窗口边框
    u32 border_inactive;        // 非活动窗口边框
    
    // 按钮颜色
    u32 btn_close;              // 关闭按钮
    u32 btn_close_hover;        // 关闭按钮悬停
    u32 btn_minimize;           // 最小化按钮
    u32 btn_maximize;           // 最大化按钮
    u32 btn_icon;               // 按钮图标颜色
    u32 btn_size;               // 按钮大小
    u32 btn_gap;                // 按钮间距
    
    // 桌面背景
    u32 desktop_bg_top;         // 桌面背景渐变顶部
    u32 desktop_bg_bottom;      // 桌面背景渐变底部
    u32 desktop_solid;          // 桌面纯色背景
    
    // 阴影
    u32 shadow_color;           // 窗口阴影颜色
    u32 shadow_size;            // 阴影大小
    
    // ========== 样式枚举 ==========
    xbutton_style_t button_style;
    xborder_style_t border_style;
    
    // ========== 自定义渲染函数 ==========
    // 设置为 NULL 则使用默认实现
    xtheme_draw_decoration_fn draw_decoration;    // 绘制整个装饰（优先使用）
    xtheme_draw_border_fn draw_border;            // 绘制边框
    xtheme_draw_titlebar_fn draw_titlebar;        // 绘制标题栏背景
    xtheme_draw_title_text_fn draw_title_text;    // 绘制标题文字
    xtheme_draw_buttons_fn draw_buttons;          // 绘制按钮
    xtheme_draw_desktop_fn draw_desktop;          // 绘制桌面
    xtheme_draw_shadow_fn draw_shadow;            // 绘制阴影
    
    // ========== 用户数据 ==========
    void* user_data;            // 自定义数据（可用于主题状态）
    
} xtheme_t;

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
    
    // 主题
    xtheme_t* theme;           // 当前主题
    xtheme_id_t theme_id;      // 当前主题ID
    
} xdisplay_t;

// ========== 显示设备 ==========
typedef struct xwin_device {
    xdisplay_t* display;
} xwin_device_t;

// ========== 主题 API ==========

// 获取主题
xtheme_t* xtheme_get(xtheme_id_t id);

// 设置主题
void xtheme_set(xdisplay_t* disp, xtheme_id_t id);

// 获取当前主题
xtheme_t* xtheme_current(xdisplay_t* disp);

// 应用主题（重绘所有窗口）
void xtheme_apply(xdisplay_t* disp);

// 主题渲染入口函数（内部使用）
void xtheme_render_decoration(xdisplay_t* disp, xwindow_t* win,
                               u32* buffer, u32 buf_w, u32 buf_h,
                               i32 sx, i32 sy, i32 ex, i32 ey);

void xtheme_render_desktop(xdisplay_t* disp, xwindow_t* desktop,
                           u32* buffer, u32 buf_w, u32 buf_h);

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

// syscall
long xwin_syscall_handler(u32 num, long a1, long a2, long a3, long a4, long a5);
void xwin_register_syscall(void);

#endif
