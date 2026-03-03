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




// 通过系统调用写入LCD填充矩形
void lcd_fill_rect_syscall(int x, int y, int w, int h, u16 color) {
     
        // 打开LCD设备
    int fd = syscall2(SYS_OPEN, "/dev/lcd", 0);
    if (fd < 0) {
        kprintf("open /dev/lcd failed!\n");
        return;
    }
    
    // 构造填充命令: "FILL x y w h color\n"
    char cmd_buf[64];
    sprintf(cmd_buf, "FILL %d %d %d %d %d\n", x, y, w, h, color);
    
    // 写入命令
    syscall3(SYS_WRITE, fd, cmd_buf, kstrlen(cmd_buf));
    
    // 关闭设备
    syscall1(SYS_CLOSE, fd);
}

void lcd_fill_rect_task(void) {

#ifdef STM32F4XX
    // 延时等待LCD初始化完成
    while(1){
        sleep_ms(1000);
        
        // 填充红色矩形 (x:10-50, y:10-50)
        lcd_fill_rect_syscall(10, 10, 40, 40, 0xf800);  // RED
        
        // 填充蓝色矩形 (x:60-100, y:10-50)
        lcd_fill_rect_syscall(60, 10, 40, 40, 0x001f); // BLUE
        
        // 循环动画：移动的绿色矩形
        for (int i = 0; i < 5; i++) {
            // 绘制绿色矩形
            lcd_fill_rect_syscall(10 + i * 10, 60, 40, 40, 0x07e0); // GREEN
            sleep_ms(200);
            // 擦除（用黑色）
            lcd_fill_rect_syscall(10 + i * 10, 60, 40, 40, 0x0000); // BLACK
        }
        // 最后绘制黄色矩形
        lcd_fill_rect_syscall(60, 60, 40, 40, 0xffe0); // YELLOW
    }
#endif

    // 线程正常退出
    syscall1(SYS_EXIT, 0);
    
    // 不应该到达这里
    while(1);
}

void cmd_lcd(){
    // 创建LCD填充矩形演示线程
    thread_t* lcd_task = thread_create_name("lcd", lcd_fill_rect_task, NULL);
    if (lcd_task == NULL) {
        kprintf("create lcd task failed!\n");
        return;
    }
    thread_run(lcd_task);
}

void t1_task(void) {

    while(1){
        sleep_ms(1000);
    }
}

void t2_task(void) {
 while(1){
    sleep_ms(1000);
    }
}

void cmd_t1(){
    // 创建LCD填充矩形演示线程
    for(int i=0;i<10;i++){
    thread_t* lcd_task = thread_create_name("t1", t1_task, NULL);
    if (lcd_task == NULL) {
        kprintf("create t1 task failed!\n");
        return;
    }
    thread_run(lcd_task);
}
}

void cmd_t2(){
    // 创建LCD填充矩形演示线程
    thread_t* lcd_task = thread_create_name("t2", t2_task, NULL);
    if (lcd_task == NULL) {
        kprintf("create t2 task failed!\n");
        return;
    }
    thread_run(lcd_task);
}


// 命令表
static cmd_entry_t cmd_table[] = {
    {"lcd", cmd_lcd},
    {"t1", cmd_t1},
    {"t2", cmd_t2},
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