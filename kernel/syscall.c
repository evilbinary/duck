/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "syscall.h"
#include "thread.h" /* thread_current()：诊断需要记下是哪个线程发的系统调用 */

static void** syscall_table = NULL;
int syscall_total = 0;

sys_fn_handler_fail_t syscall_faild_handler_fn = NULL;
sys_fn_handler_t syscall_handler_fn = NULL;

/* 【诊断用】最近 32 次系统调用记录（环形缓冲）。
 * 动机：miyoo 上 /bin/ls 崩在 dfar=0xfffffffc（= -4）。这类"解引用负值"通常是
 * **某个 syscall 返回了负错误码被用户程序当成指针/结构体**（也见上面那条：
 * 走"did not found/失败"分支时若没人写 r0，用户拿到的就是残留值）。
 * 崩溃时把最近这串调用号打出来，一眼就能看出崩之前调了哪个系统调用。 */
#define SYS_TRACE_N 32
static u32 sys_trace_no[SYS_TRACE_N];
static int sys_trace_ret[SYS_TRACE_N];
static u32 sys_trace_tid[SYS_TRACE_N];
static u32 sys_trace_idx = 0;

void sys_trace_dump(void) {
  u32 i;
  log_info("syscall trace (last %d, oldest first):\n", SYS_TRACE_N);
  for (i = 0; i < SYS_TRACE_N; i++) {
    u32 p = (sys_trace_idx + i) % SYS_TRACE_N;
    if (sys_trace_no[p] == 0xFFFFFFFFu) {
      continue;
    }
    /* tid 必须记录：全局环形会混入其它线程/进程的调用（实测无 tid 时看到的是
     * clock_nanosleep(230)/wait4(114)/read(3) 这类"shell 循环"，会误导判断）；
     * ret 同样关键：-4/-EINTR 之类负值被用户程序当指针用就会崩在 0xfffffffc。 */
    log_info("  no=%d ret=%d tid=%d\n", sys_trace_no[p], sys_trace_ret[p],
             sys_trace_tid[p]);
  }
  log_info("syscall trace end\n");
}

void* do_syscall(interrupt_context_t* ic) {
  // kprintf("syscall %d\n", context_fn(ic));
  int no = context_fn(ic);
  if (no >= 0 && no < SYSCALL_NUMBER && syscall_handler_fn != NULL) {
    syscall_handler_fn(no, ic);
  } else if (syscall_faild_handler_fn != NULL) {
    int ret = syscall_faild_handler_fn(no, ic);
    (void)ret;
  } else {
    log_warn("syscall did not found %d\n", context_fn(ic));
  }
  /* 【诊断】在 handler 跑完之后记录（此时 r0 里才是真正的返回值）。 */
  {
    thread_t* t = thread_current();
    sys_trace_no[sys_trace_idx] = (u32)no;
    sys_trace_ret[sys_trace_idx] = (int)context_ret(ic);
    sys_trace_tid[sys_trace_idx] = (u32)(t != NULL ? t->id : 0);
    sys_trace_idx = (sys_trace_idx + 1) % SYS_TRACE_N;
  }
  /* 必须返回 ic：SVC 出口用 interrupt_exit_ret，r0 为恢复栈帧 */
  return ic;
}

void sys_fn_regist_faild(void* fn) {
  if (fn == NULL) return;
  syscall_faild_handler_fn = fn;
}

void sys_fn_regist_handler(void* fn) {
  if (fn == NULL) return;
  syscall_handler_fn = fn;
}

void* sys_fn_get_handler() { return syscall_handler_fn; }

void syscall_init() {
  u32 i;
  for (i = 0; i < SYS_TRACE_N; i++) {
    sys_trace_no[i] = 0xFFFFFFFFu;
  }
  exception_regist(EX_SYS_CALL, do_syscall);
}