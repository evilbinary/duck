/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 *
 * 通用等待协议（内核层）。
 *
 * 本内核的 syscall 不能"睡在内核里"（运行帧在共享栈区，内核态被换出后
 * 调用链会被别的线程覆盖），所有阻塞都走同一套协议：
 *
 *   登记 recheck → THREAD_WAITING → schedule_switch（内核链被弃）
 *   唤醒方调用 thread_event_notify() 做一次全表重算，就绪者由
 *   thread_wake() 把结果写进保存帧 x0 并置 RUNNING。
 *
 *   recheck(arg) > 0 ：就绪（数字只表示"有事件"，具体返回值由 result 决定）
 *   recheck(arg) == 0：未就绪，继续等
 *   recheck(arg) < 0 ：放弃等待，该值直接作为 syscall 返回值
 *   result(arg)      ：可选，就绪时真正的返回值（read 的 0 字节 EOF 要用）
 *   release(arg)     ：可选，清槽时释放 arg
 *
 * 为什么放在内核而不是 posix 模块：等待是内核语义，pty / 端点 / 设备驱动 /
 * 将来的 IRQ 事件对象都要用。放在 posix 里会让内核模块反向依赖 posix，
 * 唤醒只能靠 thread_event_notify_hook 函数指针转发。
 *
 * 【与 kernel/event.c 的分工】两者都不是"信号量"，但职责不同：
 *   · event.c           —— I/O 事件源（source_t）到线程的一次性唤醒，
 *                          由驱动在中断/轮询里 event_wakeup_io() 触发；
 *   · thread_event.c    —— 条件等待（wait + notify 广播）：等待者自己提供
 *                          recheck 判定"我的条件满足了吗"，notify 方不知道
 *                          有谁在等。适合端点队列、socket 缓冲这类共享状态。
 ********************************************************************/
#ifndef KERNEL_THREAD_EVENT_H
#define KERNEL_THREAD_EVENT_H

#include "thread.h"

/* 最多同时挂起的等待者数量（每个线程占一项）。 */
#define THREAD_EVENT_MAX 32

/* 事件对象：发布方自己持有它，等待方挂上来，notify 时只扫这条链。
 * 典型用法（能力端点）：endpoint 里放一个，send/recv 改队列后
 * thread_event_notify(&ep->ev)，等待者用 thread_event_wait_on(&ep->ev, ...)。
 * 用 THREAD_EVENT_INIT 初始化（head = -1 表示空链）。 */
typedef struct thread_event {
  int head; /* wait_tab 下标链头，-1 = 没有等待者 */
} thread_event_t;

#define THREAD_EVENT_INIT \
  { -1 }

/* 挂起直到 recheck 就绪（不进任何事件的链，只能被全表广播唤醒）。
 *   recheck 会 copy_to_user ⇒ 唤醒方在【等待者页表】下调它（见 .c 注释）。
 *   has_tmo=0 表示无限等；has_tmo=1 时 tmo_ms 毫秒后按超时唤醒（返回值 0）。
 * 返回：本来/刚刚就绪时返回结果；真挂起后【不返回】（唤醒方写保存帧）。 */
int thread_event_wait(int (*recheck)(void* arg), long (*result)(void* arg),
                      void (*release)(void* arg), void* arg, u32 tmo_ms,
                      int has_tmo);

/* 同上，但登记到指定事件：只有 thread_event_notify(ev) 会唤醒它。 */
int thread_event_wait_on(thread_event_t* ev, int (*recheck)(void* arg),
                         long (*result)(void* arg), void (*release)(void* arg),
                         void* arg, u32 tmo_ms, int has_tmo);

/* 按事件唤醒：只重算注册在该事件上的等待者。ev == NULL 退化为全表广播。 */
void thread_event_notify(thread_event_t* ev);

/* 指定线程唤醒：条件不满足 / 该线程没在等 时什么都不做。返回 1 = 已唤醒。 */
int thread_event_notify_thread(thread_t* t);

/* 全表广播：发布方不知道谁在等时用（设备 IRQ、pipe、pty）。 */
void thread_event_notify_all(void);

/* 安装 thread_event_fill_hook（把超时/信号/就绪结果写回保存帧）。 */
void thread_event_init(void);

#endif
