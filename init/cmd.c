#include "main.h"
#include "modules/lcd/st7735.h"
#include "kernel/string.h"
#include "init.h"


typedef struct {
    const char* name;
    void (*func)(void);
} cmd_entry_t;


void sleep_ms(int ms) {
    struct timespec tv;
    tv.tv_nsec = (ms % 1000) * 1000 * 1000;
    tv.tv_sec = ms / 1000;
    syscall4(SYS_CLOCK_NANOSLEEP, 0, 0, &tv, &tv);
}

void lcd_fill_rect_task(void) {

#ifdef STM32F4XX
    // 延时等待LCD初始化完成
    sleep_ms(1000);
    
    // 填充红色矩形 (x:10-50, y:10-50)
    st7735_fill(10, 10, 50, 50, 0xf800);  // RED
    
    // 填充蓝色矩形 (x:60-100, y:10-50)
    st7735_fill(60, 10, 100, 50, 0x001f); // BLUE
    
    // 循环动画：移动的绿色矩形
    for (int i = 0; i < 5; i++) {
        // 绘制绿色矩形
        st7735_fill(10 + i * 10, 60, 50 + i * 10, 100, 0x07e0); // GREEN
        sleep_ms(200);
        // 擦除（用黑色）
        st7735_fill(10 + i * 10, 60, 50 + i * 10, 100, 0x0000); // BLACK
    }
    // 最后绘制黄色矩形
    st7735_fill(60, 60, 100, 100, 0xffe0); // YELLOW
#endif

    syscall1(SYS_EXIT, 0);
}

void cmd_lcd(){

    // 创建LCD填充矩形演示线程
    thread_t* lcd_task = thread_create_name("lcd_rect", lcd_fill_rect_task, NULL);
    thread_run(lcd_task);

}

// 命令表
static cmd_entry_t cmd_table[] = {
    {"lcd", cmd_lcd},
    {NULL, NULL}
};

// 根据命令名查找并执行对应函数
int cmd_exec(const char* name) {
    for (int i = 0; cmd_table[i].name != NULL; i++) {
        if (kstrcmp(name, cmd_table[i].name) == 0) {
            cmd_table[i].func();
            return 1;
        }
    }
    return -1;  // 命令未找到
}