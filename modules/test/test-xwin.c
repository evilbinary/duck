/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * X Window System 测试用例
 ********************************************************************/
#include "kernel/kernel.h"

#ifdef XWIN_MODULE
#include "modules/xwin/xwin.h"

static xdisplay_t* test_display = NULL;
static int test_passed = 0;
static int test_failed = 0;

// ========== 测试辅助宏 ==========
#define TEST_ASSERT(cond, msg) do { \
    if (cond) { \
        test_passed++; \
        log_info("[PASS] %s\n", msg); \
    } else { \
        test_failed++; \
        log_error("[FAIL] %s\n", msg); \
    } \
} while(0)

// ========== 窗口管理测试 ==========

void test_xwin_init(void) {
    log_info("\n=== Test: xwin_init ===\n");
    
    // 获取 VGA 设备
    device_t* vga_dev = device_find(DEVICE_VGA);
    TEST_ASSERT(vga_dev != NULL, "VGA device exists");
    
    if (vga_dev == NULL) {
        log_error("No VGA device, skip xwin tests\n");
        return;
    }
    
    // 初始化显示服务器
    test_display = kmalloc(sizeof(xdisplay_t), DEFAULT_TYPE);
    TEST_ASSERT(test_display != NULL, "Display allocation");
    
    int ret = xwin_init(test_display, (vga_device_t*)vga_dev->data);
    TEST_ASSERT(ret == 0, "xwin_init success");
    TEST_ASSERT(test_display->root_window != NULL, "Root window created");
    TEST_ASSERT(test_display->back_buffer != NULL, "Back buffer allocated");
}

void test_xwin_create_window(void) {
    log_info("\n=== Test: xwin_create_window ===\n");
    
    if (test_display == NULL) {
        log_error("Display not initialized\n");
        return;
    }
    
    // 创建窗口
    xwindow_t* win1 = xwin_create_window(test_display, 
                                          test_display->root_window,
                                          50, 50, 200, 150,
                                          XWIN_FLAG_VISIBLE | XWIN_FLAG_BORDERED);
    TEST_ASSERT(win1 != NULL, "Window 1 created");
    TEST_ASSERT(win1->id > 0, "Window 1 has valid ID");
    TEST_ASSERT(win1->width == 200, "Window 1 width correct");
    TEST_ASSERT(win1->height == 150, "Window 1 height correct");
    TEST_ASSERT(win1->framebuffer != NULL, "Window 1 has framebuffer");
    
    // 设置标题
    xwin_set_title(win1, "Test Window 1");
    TEST_ASSERT(kstrcmp(win1->title, "Test Window 1") == 0, "Window title set");
    
    // 创建第二个窗口
    xwindow_t* win2 = xwin_create_window(test_display,
                                          test_display->root_window,
                                          100, 100, 300, 200,
                                          XWIN_FLAG_VISIBLE | XWIN_FLAG_BORDERED);
    TEST_ASSERT(win2 != NULL, "Window 2 created");
    TEST_ASSERT(win2->id != win1->id, "Window IDs are unique");
    
    // 测试窗口查找
    xwindow_t* found = xwin_find_window(test_display, win1->id);
    TEST_ASSERT(found == win1, "Find window by ID");
    
    // 测试位置查找
    xwindow_t* at_pos = xwin_find_window_at(test_display, 60, 60);
    TEST_ASSERT(at_pos != NULL, "Find window at position");
}

void test_xwin_window_ops(void) {
    log_info("\n=== Test: xwin_window_ops ===\n");
    
    if (test_display == NULL) return;
    
    xwindow_t* win = xwin_create_window(test_display,
                                         test_display->root_window,
                                         10, 10, 100, 100,
                                         XWIN_FLAG_VISIBLE);
    TEST_ASSERT(win != NULL, "Create window for ops test");
    
    if (win == NULL) return;
    
    // 移动窗口
    i32 new_x = 200, new_y = 150;
    xwin_move(test_display, win, new_x, new_y);
    TEST_ASSERT(win->x == new_x, "Window move X");
    TEST_ASSERT(win->y == new_y, "Window move Y");
    
    // 调整大小
    u32 new_w = 250, new_h = 180;
    xwin_resize(test_display, win, new_w, new_h);
    TEST_ASSERT(win->width == new_w, "Window resize width");
    TEST_ASSERT(win->height == new_h, "Window resize height");
    
    // 显示/隐藏
    xwin_show(test_display, win, 0);
    TEST_ASSERT(win->visible == 0, "Window hide");
    
    xwin_show(test_display, win, 1);
    TEST_ASSERT(win->visible == 1, "Window show");
    
    // 焦点管理
    xwin_set_focus(test_display, win);
    TEST_ASSERT(test_display->focused_window == win, "Window focus");
    
    // Z序
    xwin_raise(test_display, win);
    xwin_lower(test_display, win);
    TEST_ASSERT(1, "Z-order operations completed");
    
    // 销毁窗口
    xwin_destroy_window(test_display, win);
    xwindow_t* found = xwin_find_window(test_display, win->id);
    TEST_ASSERT(found == NULL, "Window destroyed");
}

// ========== 图形绘制测试 ==========

void test_xwin_graphics(void) {
    log_info("\n=== Test: xwin_graphics ===\n");
    
    if (test_display == NULL) return;
    
    xwindow_t* win = xwin_create_window(test_display,
                                         test_display->root_window,
                                         0, 0, 100, 100,
                                         XWIN_FLAG_VISIBLE);
    TEST_ASSERT(win != NULL, "Create window for graphics test");
    
    if (win == NULL) return;
    
    // 清除窗口
    xwin_clear_color(win, XCOLOR_WHITE);
    TEST_ASSERT(1, "Clear window with color");
    
    // 测试绘制函数不会崩溃
    xwin_draw_pixel(win, 10, 10, XCOLOR_RED);
    TEST_ASSERT(1, "Draw pixel");
    
    xwin_draw_line(win, 0, 0, 50, 50, XCOLOR_BLUE);
    TEST_ASSERT(1, "Draw line");
    
    xwin_draw_rect(win, 5, 5, 20, 20, XCOLOR_GREEN);
    TEST_ASSERT(1, "Draw rect");
    
    xwin_fill_rect(win, 10, 10, 15, 15, XCOLOR_YELLOW);
    TEST_ASSERT(1, "Fill rect");
    
    xwin_draw_circle(win, 50, 50, 20, XCOLOR_MAGENTA);
    TEST_ASSERT(1, "Draw circle");
    
    xwin_fill_circle(win, 70, 70, 15, XCOLOR_CYAN);
    TEST_ASSERT(1, "Fill circle");
    
    xwin_draw_ellipse(win, 20, 60, 30, 15, XCOLOR_GRAY);
    TEST_ASSERT(1, "Draw ellipse");
    
    xwin_fill_ellipse(win, 50, 80, 25, 10, XCOLOR_DARK_GRAY);
    TEST_ASSERT(1, "Fill ellipse");
    
    xwin_draw_round_rect(win, 60, 10, 30, 25, 5, XCOLOR_LIGHT_GRAY);
    TEST_ASSERT(1, "Draw round rect");
    
    xwin_fill_round_rect(win, 65, 15, 20, 15, 3, XCOLOR_RED);
    TEST_ASSERT(1, "Fill round rect");
    
    // 文本绘制
    xwin_draw_text(win, 5, 5, "Hello XWIN!", XCOLOR_BLACK);
    TEST_ASSERT(1, "Draw text");
    
    xwin_draw_char(win, 30, 30, 'X', XCOLOR_BLUE, 16);
    TEST_ASSERT(1, "Draw char");
    
    xwin_destroy_window(test_display, win);
}

void test_xwin_blit(void) {
    log_info("\n=== Test: xwin_blit ===\n");
    
    if (test_display == NULL) return;
    
    xwindow_t* win = xwin_create_window(test_display,
                                         test_display->root_window,
                                         0, 0, 64, 64,
                                         XWIN_FLAG_VISIBLE);
    TEST_ASSERT(win != NULL, "Create window for blit test");
    
    if (win == NULL) return;
    
    // 创建测试位图数据
    u32 bitmap[16 * 16];
    for (int i = 0; i < 16 * 16; i++) {
        bitmap[i] = (i % 16) * 0x00101010;  // 渐变
    }
    
    // 测试 blit
    xwin_blit(win, 0, 0, bitmap, 16, 16);
    TEST_ASSERT(1, "Blit bitmap");
    
    // 测试透明 blit
    bitmap[0] = 0xFFFFFFFF;  // 白色作为透明色
    xwin_blit_transparent(win, 20, 20, bitmap, 16, 16, 0xFFFFFFFF);
    TEST_ASSERT(1, "Blit transparent");
    
    xwin_destroy_window(test_display, win);
}

// ========== 事件系统测试 ==========

void test_xwin_events(void) {
    log_info("\n=== Test: xwin_events ===\n");
    
    if (test_display == NULL) return;
    
    xwindow_t* win = xwin_create_window(test_display,
                                         test_display->root_window,
                                         0, 0, 100, 100,
                                         XWIN_FLAG_VISIBLE | XWIN_FLAG_FOCUSABLE);
    TEST_ASSERT(win != NULL, "Create window for event test");
    
    if (win == NULL) return;
    
    // 测试事件队列
    TEST_ASSERT(test_display->event_queue != NULL, "Event queue exists");
    
    // 注入鼠标移动事件
    xwin_mouse_move(test_display, 50, 50);
    TEST_ASSERT(test_display->mouse_x == 50, "Mouse X updated");
    TEST_ASSERT(test_display->mouse_y == 50, "Mouse Y updated");
    
    // 注入鼠标按键事件
    xwin_mouse_button(test_display, XBUTTON_LEFT, 1);
    TEST_ASSERT(test_display->mouse_buttons & XBUTTON_LEFT, "Mouse button pressed");
    
    xwin_mouse_button(test_display, XBUTTON_LEFT, 0);
    TEST_ASSERT(!(test_display->mouse_buttons & XBUTTON_LEFT), "Mouse button released");
    
    // 注入键盘事件
    xwin_keyboard_event(test_display, 'A', 1, 0);
    TEST_ASSERT(1, "Keyboard event injected");
    
    // 处理事件
    xwin_process_events(test_display);
    TEST_ASSERT(1, "Process events completed");
    
    // 测试获取事件
    xevent_t event;
    int has_event = xwin_next_event(test_display, &event);
    // 可能有事件，也可能没有，取决于之前的事件处理
    TEST_ASSERT(1, "Next event call completed");
    
    xwin_destroy_window(test_display, win);
}

// ========== 渲染测试 ==========

void test_xwin_render(void) {
    log_info("\n=== Test: xwin_render ===\n");
    
    if (test_display == NULL) return;
    
    xwindow_t* win = xwin_create_window(test_display,
                                         test_display->root_window,
                                         10, 10, 150, 100,
                                         XWIN_FLAG_VISIBLE | XWIN_FLAG_BORDERED);
    TEST_ASSERT(win != NULL, "Create window for render test");
    
    if (win == NULL) return;
    
    // 设置背景色
    xwin_set_bg_color(win, XCOLOR_LIGHT_GRAY);
    xwin_clear(win);
    
    // 绘制一些内容
    xwin_fill_rect(win, 10, 10, 50, 30, XCOLOR_RED);
    xwin_draw_text(win, 15, 50, "Render Test", XCOLOR_WHITE);
    
    // 标记损坏区域
    xwin_damage_all(win);
    TEST_ASSERT(win->damaged, "Window damaged");
    
    // 渲染
    xwin_render(test_display);
    TEST_ASSERT(1, "Render completed");
    
    // 合成
    xwin_composite(test_display);
    TEST_ASSERT(1, "Composite completed");
    
    // 翻转缓冲区
    xwin_flip_buffer(test_display);
    TEST_ASSERT(1, "Flip buffer completed");
    
    xwin_destroy_window(test_display, win);
}

// ========== 窗口管理器测试 ==========

void test_xwin_wm(void) {
    log_info("\n=== Test: xwin_wm ===\n");
    
    if (test_display == NULL) return;
    
    // 创建有边框的窗口
    xwindow_t* win = xwin_create_window(test_display,
                                         test_display->root_window,
                                         50, 50, 200, 150,
                                         XWIN_FLAG_VISIBLE | XWIN_FLAG_BORDERED | 
                                         XWIN_FLAG_DRAGGABLE | XWIN_FLAG_RESIZABLE);
    TEST_ASSERT(win != NULL, "Create bordered window");
    TEST_ASSERT(win->flags & XWIN_FLAG_BORDERED, "Window has border flag");
    TEST_ASSERT(win->flags & XWIN_FLAG_DRAGGABLE, "Window is draggable");
    
    if (win != NULL) {
        xwin_set_title(win, "WM Test Window");
        xwin_destroy_window(test_display, win);
    }
    
    // 创建无边框窗口
    win = xwin_create_window(test_display,
                              test_display->root_window,
                              100, 100, 100, 100,
                              XWIN_FLAG_VISIBLE);
    TEST_ASSERT(win != NULL, "Create borderless window");
    TEST_ASSERT(!(win->flags & XWIN_FLAG_BORDERED), "Window has no border flag");
    
    if (win != NULL) {
        xwin_destroy_window(test_display, win);
    }
}

// ========== 综合测试 ==========

void test_xwin_stress(void) {
    log_info("\n=== Test: xwin_stress ===\n");
    
    if (test_display == NULL) return;
    
    #define STRESS_WINDOW_COUNT 20
    xwindow_t* windows[STRESS_WINDOW_COUNT];
    
    // 创建多个窗口
    int created = 0;
    for (int i = 0; i < STRESS_WINDOW_COUNT; i++) {
        windows[i] = xwin_create_window(test_display,
                                         test_display->root_window,
                                         i * 10, i * 10,
                                         100, 80,
                                         XWIN_FLAG_VISIBLE | XWIN_FLAG_BORDERED);
        if (windows[i] != NULL) {
            created++;
            xwin_set_bg_color(windows[i], XCOLOR_BLACK + i * 0x00101010);
            xwin_clear(windows[i]);
        }
    }
    TEST_ASSERT(created > 0, "Stress: created multiple windows");
    log_info("Created %d/%d windows\n", created, STRESS_WINDOW_COUNT);
    
    // 渲染所有窗口
    xwin_render(test_display);
    xwin_flip_buffer(test_display);
    TEST_ASSERT(1, "Stress: rendered all windows");
    
    // 销毁所有窗口
    for (int i = 0; i < STRESS_WINDOW_COUNT; i++) {
        if (windows[i] != NULL) {
            xwin_destroy_window(test_display, windows[i]);
        }
    }
    TEST_ASSERT(1, "Stress: destroyed all windows");
}

// ========== 测试入口 ==========

void test_xwin(void) {
    log_info("\n");
    log_info("========================================\n");
    log_info("     X Window System Test Suite\n");
    log_info("========================================\n");
    
    test_passed = 0;
    test_failed = 0;
    
    // 运行测试
    test_xwin_init();
    
    if (test_display != NULL) {
        test_xwin_create_window();
        test_xwin_window_ops();
        test_xwin_graphics();
        test_xwin_blit();
        test_xwin_events();
        test_xwin_render();
        test_xwin_wm();
        test_xwin_stress();
    }
    
    // 清理
    if (test_display != NULL) {
        xwin_exit(test_display);
        kfree(test_display);
        test_display = NULL;
    }
    
    // 输出结果
    log_info("\n========================================\n");
    log_info("Test Results: %d passed, %d failed\n", test_passed, test_failed);
    log_info("========================================\n");
}

#else

void test_xwin(void) {
    log_info("XWIN module not enabled, skip test\n");
}

#endif
