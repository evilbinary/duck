/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * 信号 handler 的真实执行：信号帧 + rt_sigreturn。
 *
 * 【整体协议】
 *   · 投递点：内核在"即将 eret 回用户态"时统一调 thread_user_return_run()
 *     （通用钩子，见 kernel/thread.h；两个落点：exception_process 的
 *     syscall/IRQ 返回、schedule_switch 的阻塞唤醒返回）。本文件把
 *     signal_user_return() 挂到 thread_user_return_hook 上 —— 内核完全不知道
 *     "信号"这个概念，只负责最后一刻叫一声。
 *   · 建帧：把当前现场（pc/psr/sp/x0..x30 + 屏蔽字）整份存到用户栈上，
 *     然后把返回现场改成"调用 handler"：
 *        x0 = signum、x1 = &siginfo、x2 = &ucontext、
 *        lr = sa_restorer（musl 的 __restore 就是 `mov x8,#139; svc 0`）、
 *        pc = handler、sp = 帧基址。
 *   · 恢复：handler 末尾的 restorer 发 SYS_rt_sigreturn(139)，本文件把帧里的
 *     现场写回当前 trap 帧 —— 于是用户态从被中断处继续，返回值/寄存器全都
 *     和"没被信号打断过"一样。
 *
 * 【为什么不按 Linux 的 rt_sigframe 布局】
 *   帧的读写两端都在内核里（建帧在投递点、解帧在 sigreturn），只有
 *   siginfo/ucontext 两块要按 ABI 布局给用户 handler 看。所以帧头用自定义
 *   布局（带 magic 校验）最稳，也不会被 libc 的 setcontext 约定绑住。
 ********************************************************************/
#include "sysfn.h"

#include "kernel/error.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "kernel/thread.h"

#define log_debug

/* 【信号帧】必须在用户栈上 16 字节对齐：base = (sp & ~0xF) - SIGFRAME_SIZE
 * SIGFRAME_SIZE 是 16 的倍数 ⇒ base 仍然 16 对齐（aarch64 栈对齐要求）。 */
#define SIGFRAME_MAGIC 0x53534C46ull /* 'F','L','S','S' —— magic 用小端读 */
#define SIGFRAME_SIGINFO_OFF 312
#define SIGFRAME_UCONTEXT_OFF 440
#define SIGFRAME_SIZE 960

struct sigframe_k {
  u64 magic;
  u64 sig;
  u64 psr;   /* ---- 以下是要恢复的现场 ---- */
  u64 pc;
  u64 sp;
  u64 lr;
  u64 mask;  /* handler 返回后要恢复的信号屏蔽字 */
  u64 flags; /* sa_flags（SA_RESTART / SA_NODEFER …），恢复时不用，便于诊断 */
  u64 regs[31]; /* 64：x0..x30（x30 与 lr 相同） */
  u8 siginfo[128];  /* 312：交给 handler 的第二个参数 */
  u8 ucontext[512]; /* 440：第三个参数（只填到 sigcontext.pstate） */
  u64 pad;          /* 保证 sizeof == SIGFRAME_SIZE */
};

/* sizeof 必须与上面的常量一致（帧偏移是写死的，差一拍就会把 handler 的
 * siginfo 指到垃圾上）。用编译期断言锁死。 */
typedef char sigframe_size_check[(sizeof(struct sigframe_k) == SIGFRAME_SIZE)
                                     ? 1
                                     : -1];

#define SA_NODEFER_ 0x40000000u

/* 【兜底 restorer 由架构决定，本文件不写任何汇编】正常 libc 用 SA_RESTORER
 * 传自己的 __restore；用裸 syscall 装 handler 的程序（LTP 的
 * rt_sigaction01/02/03）不给 restorer，真 Linux 用 vDSO 的
 * __kernel_rt_sigreturn 兜底。本内核没有 vDSO ⇒ 交给
 * cpu_signal_restorer()（arch/cpu.h）：
 *   · armv8-a 给强实现（用户跑 EL1t，可执行内核里的桩）；
 *   · 其它架构弱符号返回 0 ⇒ 这里放弃投递并消费位图（绝不能让用户跳到
 *     内核代码里，那些平台会直接 fault）。 */

/* siginfo_t 前三个字段（musl/glibc/内核 uapi 一致）：
 *   int si_signo; int si_errno; int si_code; */
static void sigframe_fill_siginfo(struct sigframe_k* fr, int sig) {
  u32* si = (u32*)fr->siginfo;
  si[0] = (u32)sig; /* si_signo */
  si[1] = 0;        /* si_errno */
  si[2] = 0;        /* si_code = SI_USER */
}

/* ucontext_t 前缀（按 musl aarch64 的布局：uc_flags/uc_link/uc_stack/
 * mcontext(sigcontext)。sigcontext = fault_address + regs[31] + sp/pc/pstate。
 * 再往后的 __reserved[256]（4KB 的 FP/SVE 区）不填，handler 读到的就是 0。 */
static void sigframe_fill_ucontext(struct sigframe_k* fr,
                                   interrupt_context_t* ic) {
  u64* uc = (u64*)fr->ucontext;
  int i;
  uc[0] = 0; /* uc_flags */
  uc[1] = 0; /* uc_link */
  /* [2..4] = uc_stack（ss_sp/size/flags，留 0） */
  uc[5] = 0; /* sigcontext.fault_address */
  for (i = 0; i < 31; i++) {
    uc[6 + i] = fr->regs[i]; /* sigcontext.regs[0..30] */
  }
  uc[37] = ic->sp;  /* sigcontext.sp      = 296/8 */
  uc[38] = ic->pc;  /* sigcontext.pc      = 304/8 */
  uc[39] = ic->psr; /* sigcontext.pstate  = 312/8 */
}

static void sigframe_save_regs(struct sigframe_k* fr, interrupt_context_t* ic) {
  fr->regs[0] = ic->x0;
  fr->regs[1] = ic->x1;
  fr->regs[2] = ic->x2;
  fr->regs[3] = ic->x3;
  fr->regs[4] = ic->x4;
  fr->regs[5] = ic->x5;
  fr->regs[6] = ic->x6;
  fr->regs[7] = ic->x7;
  fr->regs[8] = ic->x8;
  fr->regs[9] = ic->x9;
  fr->regs[10] = ic->x10;
  fr->regs[11] = ic->x11;
  fr->regs[12] = ic->x12;
  fr->regs[13] = ic->x13;
  fr->regs[14] = ic->x14;
  fr->regs[15] = ic->x15;
  fr->regs[16] = ic->x16;
  fr->regs[17] = ic->x17;
  fr->regs[18] = ic->x18;
  fr->regs[19] = ic->x19;
  fr->regs[20] = ic->x20;
  fr->regs[21] = ic->x21;
  fr->regs[22] = ic->x22;
  fr->regs[23] = ic->x23;
  fr->regs[24] = ic->x24;
  fr->regs[25] = ic->x25;
  fr->regs[26] = ic->x26;
  fr->regs[27] = ic->x27;
  fr->regs[28] = ic->x28;
  fr->regs[29] = ic->x29;
  fr->regs[30] = ic->lr;
}

static void sigframe_restore_regs(interrupt_context_t* ic,
                                  struct sigframe_k* fr) {
  ic->x0 = fr->regs[0];
  ic->x1 = fr->regs[1];
  ic->x2 = fr->regs[2];
  ic->x3 = fr->regs[3];
  ic->x4 = fr->regs[4];
  ic->x5 = fr->regs[5];
  ic->x6 = fr->regs[6];
  ic->x7 = fr->regs[7];
  ic->x8 = fr->regs[8];
  ic->x9 = fr->regs[9];
  ic->x10 = fr->regs[10];
  ic->x11 = fr->regs[11];
  ic->x12 = fr->regs[12];
  ic->x13 = fr->regs[13];
  ic->x14 = fr->regs[14];
  ic->x15 = fr->regs[15];
  ic->x16 = fr->regs[16];
  ic->x17 = fr->regs[17];
  ic->x18 = fr->regs[18];
  ic->x19 = fr->regs[19];
  ic->x20 = fr->regs[20];
  ic->x21 = fr->regs[21];
  ic->x22 = fr->regs[22];
  ic->x23 = fr->regs[23];
  ic->x24 = fr->regs[24];
  ic->x25 = fr->regs[25];
  ic->x26 = fr->regs[26];
  ic->x27 = fr->regs[27];
  ic->x28 = fr->regs[28];
  ic->x29 = fr->regs[29];
  ic->lr = fr->regs[30];
}

/* ===================== 投递（建帧 + 改现场） ===================== */

/* 取"下一个该处理的信号号"：跳过被屏蔽的，顺手把 SIG_IGN/SIG_DFL 消费掉。
 * 返回 0 表示没有可投递的 handler 型信号。 */
static int signal_pick(thread_t* t, struct thread_sigaction* acts) {
  int guard;
  for (guard = 0; guard < THREAD_NSIG; guard++) {
    u64 avail = t->sig_pending & ~t->sig_blocked;
    int sig = 0;
    int i;
    if (avail == 0) {
      return 0;
    }
    for (i = 1; i < THREAD_NSIG; i++) {
      if (avail & THREAD_SIGBIT(i)) {
        sig = i;
        break;
      }
    }
    if (sig == 0) {
      return 0;
    }
    if (acts != NULL && (u64)acts[sig].handler > 1) {
      return sig; /* 有 handler：交给调用方建帧 */
    }
    /* SIG_IGN(1) / SIG_DFL(0)：本内核不实现默认动作（终止/忽略/继续），
     * 但必须消费掉位图 —— 否则它会一直挂在 pending 上，让每一次回用户态都
     * 重复"投递"一次。Linux 上这类信号本来就不会残留 pending。 */
    t->sig_pending &= ~THREAD_SIGBIT(sig);
  }
  return 0;
}

/* 【回用户态钩子的实现】挂在 thread_user_return_hook 上（见 kernel/thread.h）：
 * 内核只说"马上要 eret 了，给你最后一次改帧的机会"，这里判断有没有待投递的
 * handler 型信号，有就在用户栈建帧、把 ic 改成"调用 handler"的现场。 */
static void signal_user_return(thread_t* t, void* vic) {
  interrupt_context_t* ic = (interrupt_context_t*)vic;
  struct thread_sigaction* acts;
  struct sigframe_k* fr;
  u64 handler;
  u64 restorer;
  u64 base;
  u64 newmask;
  u64 fr_sp;
  u64 fr_pc;
  int sig;

  if (t == NULL || ic == NULL || t->sig_pending == 0) {
    return;
  }
  acts = (struct thread_sigaction*)t->sig_acts;
  sig = signal_pick(t, acts);
  if (sig == 0) {
    return;
  }
  handler = (u64)acts[sig].handler;
  restorer = (u64)acts[sig].restorer;
  if (restorer == 0) {
    /* 裸 syscall 装 handler 时没有 restorer（等价真 Linux 的 vDSO
     * __kernel_rt_sigreturn）：问架构要兜底桩。拿不到就别投递。 */
    restorer = cpu_signal_restorer();
    if (restorer == 0) {
      t->sig_pending &= ~THREAD_SIGBIT(sig);
      log_error("signal %d dropped: no restorer tid=%d\n", sig, (int)t->id);
      return;
    }
  }

  fr = kmalloc(sizeof(*fr), KERNEL_TYPE);
  if (fr == NULL) {
    return;
  }
  kmemset(fr, 0, sizeof(*fr));
  fr->magic = SIGFRAME_MAGIC;
  fr->sig = (u64)sig;
  fr->psr = ic->psr;
  fr->pc = ic->pc;
  fr->sp = ic->sp;
  fr->lr = ic->lr;
  fr->mask = t->sig_blocked; /* handler 返回后恢复成"进 handler 前"的屏蔽字 */
  fr->flags = acts[sig].flags;
  sigframe_save_regs(fr, ic);
  sigframe_fill_siginfo(fr, sig);
  sigframe_fill_ucontext(fr, ic);

  fr_sp = ic->sp;
  fr_pc = ic->pc;
  base = (ic->sp & ~0xFull) - (u64)SIGFRAME_SIZE;
  if (sys_copy_to_user((void*)(uintptr_t)base, fr, sizeof(*fr)) < 0) {
    /* 用户栈不可写（栈溢出/坏 sp）：不投递，消费掉避免每次回用户态重试。 */
    kfree(fr);
    t->sig_pending &= ~THREAD_SIGBIT(sig);
    log_error("signal %d frame write failed sp=%lx tid=%d\n", sig, (u64)ic->sp,
              (int)t->id);
    return;
  }
  kfree(fr);

  /* 【mask 更新】Linux：进入 handler 时 mask |= sa_mask，且除非 SA_NODEFER
   * 否则把本信号也屏蔽（防止 handler 里递归）。sigreturn 恢复 fr->mask。 */
  newmask = t->sig_blocked | acts[sig].mask;
  if ((acts[sig].flags & SA_NODEFER_) == 0) {
    newmask |= THREAD_SIGBIT(sig);
  }
  t->sig_blocked = newmask;
  t->sig_pending &= ~THREAD_SIGBIT(sig);

  ic->x0 = (u64)sig;                          /* x0 = signum */
  ic->x1 = base + SIGFRAME_SIGINFO_OFF;       /* x1 = &siginfo */
  ic->x2 = base + SIGFRAME_UCONTEXT_OFF;      /* x2 = &ucontext */
  ic->lr = restorer;                          /* handler 返回 → restorer */
  ic->pc = handler;
  ic->sp = base;
  /* 【诊断·可删】每次投递打一行：sp/base/pc/handler/restorer 是排查"handler
   * 跑飞/栈错位"的唯一线索。 */
  log_error("SIGDEL tid=%d sig=%d sp=%lx base=%lx pc=%lx h=%lx r=%lx psr=%lx\n",
            (int)t->id, sig, (u64)fr_sp, base, (u64)fr_pc, handler, restorer,
            (u64)ic->psr);
}

/* ===================== rt_sigreturn(139) ===================== */

/* 返回类型必须是 u64！dispatch（sys_fn_call）是 u64(*)(u64×6)，返回值直接
 * 写回 x0 —— 而这里返回的正是"被信号打断时的 x0"，用 int 会把指针截断。 */
u64 sys_rt_sigreturn(void) {
  thread_t* t = thread_current();
  interrupt_context_t* ic;
  struct sigframe_k* fr;
  u64 sp;
  u64 x0;

  if (t == NULL || t->ctx == NULL) {
    return (u64)-1;
  }
  /* 当前 trap 帧：syscall 入口处 exception_process/sys_fn_call_handler 都把它
   * 记在 ctx->ic 上，SVC 出口（interrupt_exit_ret）恢复的也正是这一份。 */
  ic = (interrupt_context_t*)t->ctx->ic;
  if (ic == NULL) {
    return (u64)-1;
  }
  sp = ic->sp & ~0xFull; /* handler 进来时 sp 就是帧基址 */
  fr = kmalloc(sizeof(*fr), KERNEL_TYPE);
  if (fr == NULL) {
    return (u64)-1;
  }
  if (sys_copy_from_user(fr, (const void*)(uintptr_t)sp, sizeof(*fr)) < 0 ||
      fr->magic != SIGFRAME_MAGIC) {
    kfree(fr);
    log_error("rt_sigreturn bad frame tid=%d sp=%lx\n", (int)t->id, (u64)ic->sp);
    /* Linux 对坏帧是 force SIGSEGV；这里直接结束该线程（默认动作未实现）。 */
    thread_exit(t, -1);
    return (u64)-1;
  }
  ic->psr = fr->psr;
  ic->pc = fr->pc;
  ic->sp = fr->sp;
  ic->lr = fr->lr;
  sigframe_restore_regs(ic, fr);
  t->sig_blocked = fr->mask;
  x0 = fr->regs[0]; /* dispatcher 会把返回值写回 x0 = 原值，正好无副作用 */
  kfree(fr);
  return x0;
}

/* ===================== 注册 ===================== */

void sysfn_signal_init(void) {
  /* 挂到通用的"回用户态钩子"上：内核不知道信号的存在，只负责在 eret 前叫一声。 */
  thread_user_return_hook = signal_user_return;
  log_debug("signal user-return hook registered\n");
}

