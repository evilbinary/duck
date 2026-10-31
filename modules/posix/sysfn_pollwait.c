/* poll/select/epoll/read 的阻塞等待 —— 实现已上移到内核通用层
 * （duck/kernel/wait.c、wait.h）。
 *
 * 为什么上移：等待是内核语义，pty / 能力端点 / 设备驱动 / 将来的 IRQ 事件
 * 对象都要用。放在 posix 模块里会让内核模块反向依赖 posix，唤醒也只能靠
 * thread_event_notify_hook 函数指针转发。这里只保留兼容封装，posix 内部
 * （sysfn_fs.c / sysfn_event.c）的调用点不需要改。 */
#include "sysfn.h"

#include "kernel/thread_event.h"

int poll_wait_suspend(int (*recheck)(void* arg), long (*result)(void* arg),
                      void (*release)(void* arg), void* arg, u32 tmo_ms,
                      int has_tmo) {
  return thread_event_wait(recheck, result, release, arg, tmo_ms, has_tmo);
}

void poll_wait_notify(void) { thread_event_notify_all(); }

void poll_wait_init(void) {
  thread_event_init();
  /* pidfd 退出通知（sysfn_proc.c）：目标进程退出时把状态记进 pidfd 表 */
  thread_pidfd_exit_hook = sys_pidfd_note_exit;
}
