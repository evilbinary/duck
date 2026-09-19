/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
//
// 统一中断（IRQ）核心实现 —— 设计文档 docs/develop/architecture/中断子系统设计.md
//
// 【为什么核心放在 kernel 而不是 modules】
//   1) 被异常入口（kernel/exceptions.c 的 EX_IRQ 分支）直接调用，放模块会造成
//      arch/kernel 反向依赖 module；
//   2) 必须早于 module_run_all()（app/init/module.c）就绪：schedule_init() 里的
//      timer_init() 一开就有时钟中断了；
//   3) 每个平台都必须有，不该是可裁剪项；
//   4) 静态链接下放模块并不省空间/不更快。
//   控制器驱动（GIC/PLIC/PL190/8259…）与诊断（/dev/irq）才是适合做模块的部分。
//
// 【本期（S1）状态】框架与控制器抽象已就位；只有当平台调用 irq_chip_register()
// 之后 irq_core_enabled() 才为真。未注册时 exceptions.c 仍走老的
// interrupt_get_source() 路径 ⇒ 行为与之前完全一致（其它平台零影响）。
//

#include "irq.h"

#include "exceptions.h" /* exception_handlers[] / EX_TIMER（tick 走老槽）*/
#include "kernel/kernel.h"

/* ------------------------------------------------------------------ */
/* 内部数据                                                            */
/* ------------------------------------------------------------------ */

struct irq_domain {
  struct irq_chip *chip;
  u32 base;
  u32 num;
};

static struct irq_desc desc[IRQ_NUM_MAX];
static struct irq_domain domains[IRQ_CHIP_MAX];
static int domain_count;

static struct irq_action action_pool[IRQ_ACTION_MAX];

static u32 tick_irq = 0xFFFFFFFFu; /* irq_set_tick() 之前 = 无 */
static volatile u32 in_irq;

struct bh_ent {
  irq_bh_fn fn;
  void *arg;
};
static struct bh_ent bh_q[IRQ_BH_MAX];
static volatile u32 bh_head, bh_tail;
static u32 bh_overflow;

/* ------------------------------------------------------------------ */
/* 临界区：保存/还原中断状态（不是简单的 cli/sti）                        */
/* ------------------------------------------------------------------ */
/* 【为什么不能直接用 cpu_cli()+cpu_sti()】cpu_sti() 是**无条件开中断**，而
 * irq_chip_register()/irq_register() 可能在"中断本来就被屏蔽"的早期启动阶段被调用：
 * v3s 的 timer_init()（→ irq_chip_register）位于 kernel_init() 中段，后面还有
 * memory_init/thread_init 没跑完。此时若把中断打开，时钟中断会立刻打进来，而
 * do_schedule() 在 current_thread == NULL 时是**提前返回、不调 timer_end()** 的，
 * 定时器挂起位没人清 ⇒ 电平触发的 GIC 反复重投递 ⇒ 启动被中断风暴卡死。
 * 所以这里保存 CPSR 再关中断，解锁时按原值还原（原本开着才开）。
 * ARM32 用 mrs/msr cpsr 实现；其它架构暂退化为 cpu_cli/cpu_sti（它们当前没有平台
 * 会注册 chip，等有需要时把这对函数提到 arch 层各自实现即可）。 */
#if (defined(ARMV7_A) || defined(ARMV7) || defined(ARMV5)) && !defined(ARM64)
static u32 irq_save_flags(void) {
  u32 cpsr;
  __asm__ __volatile__("mrs %0, cpsr" : "=r"(cpsr));
  __asm__ __volatile__("msr cpsr_c, %0" ::"r"(cpsr | 0xC0u)); /* I|F = 1（关 IRQ/FIQ）*/
  return cpsr;
}
static void irq_restore_flags(u32 flags) {
  __asm__ __volatile__("msr cpsr_c, %0" ::"r"(flags));
}
#else
static u32 irq_save_flags(void) {
  cpu_cli();
  return 0;
}
static void irq_restore_flags(u32 flags) {
  (void)flags;
  cpu_sti();
}
#endif

static u32 irq_lock(void) { return irq_save_flags(); }
static void irq_unlock(u32 flags) { irq_restore_flags(flags); }

static int irq_valid(u32 irq) { return irq < IRQ_NUM_MAX; }

static struct irq_chip *primary_chip(void) {
  return domain_count > 0 ? domains[0].chip : NULL;
}

static struct irq_chip *irq_to_chip(u32 irq) {
  int i;
  for (i = 0; i < domain_count; i++) {
    if (irq >= domains[i].base && irq < domains[i].base + domains[i].num) {
      return domains[i].chip;
    }
  }
  return primary_chip(); /* 平台没声明号段 ⇒ 归主控制器 */
}

/* ------------------------------------------------------------------ */
/* 控制器注册 / tick                                                   */
/* ------------------------------------------------------------------ */

int irq_chip_register(struct irq_chip *chip, u32 irq_base, u32 irq_num) {
  int i;
  u32 f;

  if (chip == NULL || irq_base >= IRQ_NUM_MAX || domain_count >= IRQ_CHIP_MAX) {
    return -1;
  }

  f = irq_lock();
  /* 【幂等】开了 MP 的平台每颗 CPU 都会走一遍启动路径（kernel_init 的 else 分支 →
   * schedule_init → timer_init），同一个 chip 会被注册多次；这里直接认作成功。 */
  for (i = 0; i < domain_count; i++) {
    if (domains[i].chip == chip) {
      irq_unlock(f);
      return 0;
    }
  }
  domains[domain_count].chip = chip;
  domains[domain_count].base = irq_base;
  domains[domain_count].num = irq_num;
  for (i = 0; i < (int)irq_num && (irq_base + i) < IRQ_NUM_MAX; i++) {
    desc[irq_base + i].chip = chip;
  }
  domain_count++;
  irq_unlock(f);

  kprintf("irq: chip '%s' registered, irq %u..%u\n", chip->name, irq_base,
          irq_base + (irq_num ? irq_num - 1 : 0));
  return 0;
}

void irq_set_tick(u32 irq) {
  tick_irq = irq;
  kprintf("irq: tick irq = %u\n", irq);
}

int irq_core_enabled(void) { return primary_chip() != NULL; }

/* ------------------------------------------------------------------ */
/* 注册 / 反注册                                                       */
/* ------------------------------------------------------------------ */

static struct irq_action *action_alloc(void) {
  int i;
  for (i = 0; i < IRQ_ACTION_MAX; i++) {
    if (action_pool[i].fn == NULL && action_pool[i].next == NULL && action_pool[i].name == NULL) {
      return &action_pool[i];
    }
  }
  return NULL;
}

static void action_free(struct irq_action *a) {
  if (a == NULL) return;
  a->fn = NULL;
  a->arg = NULL;
  a->name = NULL;
  a->next = NULL;
}

int irq_register(u32 irq, irq_handler_t fn, void *arg, const char *name, u32 flags) {
  struct irq_desc *d;
  struct irq_action *a;
  u32 f;

  if (fn == NULL || !irq_valid(irq)) return -1;

  f = irq_lock();
  d = &desc[irq];

  /* 已有注册者但不是共享中断 ⇒ 拒绝（避免历史上那种"静默覆盖"，perf 覆盖 EX_TIMER） */
  if (d->action != NULL && !(flags & IRQ_FLAG_SHARED) &&
      !(d->flags & IRQ_FLAG_SHARED)) {
    const char *owner = d->action->name;
    irq_unlock(f);
    kprintf("irq: %u already taken by '%s'\n", irq, owner ? owner : "?");
    return -2;
  }

  for (a = d->action; a != NULL; a = a->next) { /* 重复注册视为成功 */
    if (a->fn == fn && a->arg == arg) {
      irq_unlock(f);
      return 0;
    }
  }

  a = action_alloc();
  if (a == NULL) {
    irq_unlock(f);
    kprintf("irq: action pool exhausted (%d)\n", IRQ_ACTION_MAX);
    return -3;
  }

  a->fn = fn;
  a->arg = arg;
  a->name = name;

  /* 【发布顺序】先把节点内容写完，最后一步才把它挂到链上（单次指针写）：
   * 即使此刻被中断打断，handler 看到的也是"完整的新链"或"完整的旧链"。 */
  a->next = d->action;
  d->action = a;

  d->flags |= (u8)flags;
  if (d->chip == NULL) d->chip = irq_to_chip(irq);
  irq_unlock(f);

  kprintf("irq: registered %u '%s' (flags=%x)\n", irq, name ? name : "?", flags);
  return 0;
}

int irq_unregister(u32 irq, irq_handler_t fn, void *arg) {
  struct irq_desc *d;
  struct irq_action *a, *prev = NULL;
  u32 f;

  if (!irq_valid(irq)) return -1;

  f = irq_lock();
  d = &desc[irq];
  for (a = d->action; a != NULL; prev = a, a = a->next) {
    if (a->fn == fn && a->arg == arg) {
      if (prev == NULL) {
        d->action = a->next;
      } else {
        prev->next = a->next;
      }
      action_free(a);
      /* 最后一个 handler 走了：把该号屏蔽掉，避免"未注册风暴" */
      if (d->action == NULL && d->chip != NULL && d->chip->mask != NULL) {
        d->chip->mask(irq);
        d->masked = 1;
      }
      irq_unlock(f);
      return 0;
    }
  }
  irq_unlock(f);
  return -1;
}

/* ------------------------------------------------------------------ */
/* 使能 / 屏蔽 / 配置                                                  */
/* ------------------------------------------------------------------ */

static int chip_call_mask(struct irq_desc *d, u32 irq) {
  if (d->chip == NULL || d->chip->mask == NULL) return -1;
  d->chip->mask(irq);
  d->masked = 1;
  return 0;
}

static int chip_call_unmask(struct irq_desc *d, u32 irq) {
  if (d->chip == NULL || d->chip->unmask == NULL) return -1;
  d->chip->unmask(irq);
  d->masked = 0;
  return 0;
}

int irq_enable(u32 irq) {
  struct irq_desc *d;
  int r;
  u32 f;
  if (!irq_valid(irq)) return -1;
  f = irq_lock();
  d = &desc[irq];
  r = chip_call_unmask(d, irq);
  if (r == 0) d->enabled = 1;
  irq_unlock(f);
  return r;
}

int irq_disable(u32 irq) {
  struct irq_desc *d;
  int r;
  u32 f;
  if (!irq_valid(irq)) return -1;
  f = irq_lock();
  d = &desc[irq];
  d->enabled = 0;
  r = chip_call_mask(d, irq);
  irq_unlock(f);
  return r;
}

int irq_mask(u32 irq) {
  if (!irq_valid(irq)) return -1;
  return chip_call_mask(&desc[irq], irq);
}

int irq_unmask(u32 irq) {
  if (!irq_valid(irq)) return -1;
  return chip_call_unmask(&desc[irq], irq);
}

int irq_set_priority(u32 irq, u32 prio) {
  struct irq_desc *d;
  if (!irq_valid(irq)) return -1;
  d = &desc[irq];
  if (d->chip == NULL || d->chip->set_priority == NULL) return -1;
  d->chip->set_priority(irq, prio);
  return 0;
}

int irq_set_type(u32 irq, u32 type) {
  struct irq_desc *d;
  if (!irq_valid(irq)) return -1;
  d = &desc[irq];
  if (d->chip == NULL || d->chip->set_type == NULL) return -1;
  d->chip->set_type(irq, type);
  return 0;
}

int irq_set_affinity(u32 irq, u32 cpu) {
  struct irq_desc *d;
  if (!irq_valid(irq)) return -1;
  d = &desc[irq];
  if (d->chip == NULL || d->chip->set_affinity == NULL) return -1;
  d->chip->set_affinity(irq, cpu);
  return 0;
}

/* ------------------------------------------------------------------ */
/* 查询 / 诊断                                                         */
/* ------------------------------------------------------------------ */

u32 irq_get_count(u32 irq) { return irq_valid(irq) ? desc[irq].count : 0; }

const char *irq_get_name(u32 irq) {
  if (!irq_valid(irq) || desc[irq].action == NULL) return "unregistered";
  return desc[irq].action->name ? desc[irq].action->name : "?";
}

int irq_in_interrupt(void) { return in_irq != 0; }

void irq_warn_once(u32 irq) {
  if (!irq_valid(irq)) return;
  if (desc[irq].logged) return;
  desc[irq].logged = 1;
  /* 【为什么用 kprintf 不用 log_*】log_* 可能走 vfs/thread_current（非中断安全），
   * kprintf 是同步写串口，在中断里是安全的（虽然阻塞，但这里只打一次）。 */
  kprintf("irq: %u (%s) unhandled -> auto mask（仅提示一次）\n", irq,
          desc[irq].action ? desc[irq].action->name : "no handler");
}

void irq_dump(void) {
  int i, lines = 0;

  kprintf("irq   name              count  unhand  spur  stat   chip\n");
  for (i = 0; i < IRQ_NUM_MAX; i++) {
    struct irq_desc *d = &desc[i];
    if (d->action == NULL && d->count == 0 && d->unhandled == 0 &&
        (u32)i != tick_irq) {
      continue;
    }
    kprintf("%3d   %-16s %5u %6u %5u %5s  %s\n", i, irq_get_name((u32)i), d->count,
            d->unhandled, d->spurious, d->masked ? "mask" : "on",
            d->chip ? d->chip->name : "-");
    lines++;
  }
  kprintf("irq: %d line(s), chips=%d, bh_overflow=%u, in_irq=%u\n", lines, domain_count,
          bh_overflow, in_irq);
}

struct irq_desc *irq_get_desc(u32 irq) { return irq_valid(irq) ? &desc[irq] : NULL; }

/* ------------------------------------------------------------------ */
/* bottom half                                                         */
/* ------------------------------------------------------------------ */

int irq_request_bh(irq_bh_fn fn, void *arg) {
  u32 t;

  if (fn == NULL) return -1;
  t = bh_tail;
  if (((t + 1) % IRQ_BH_MAX) == bh_head) {
    /* 队列满：就地执行（语义与"直接在 ISR 里调用"一致），并计数便于发现 */
    bh_overflow++;
    fn(arg);
    return -1;
  }
  bh_q[t].fn = fn;
  bh_q[t].arg = arg;
  bh_tail = (t + 1) % IRQ_BH_MAX;
  return 0;
}

void irq_bh_poll(void) {
  int guard = 0;

  /* 仍在"进入中断"的上下文里（关中断）。BH 里再排 BH 也允许，用 guard 防止死循环。 */
  while (bh_head != bh_tail && guard++ < IRQ_BH_MAX * 2) {
    irq_bh_fn fn = bh_q[bh_head].fn;
    void *arg = bh_q[bh_head].arg;
    bh_head = (bh_head + 1) % IRQ_BH_MAX;
    if (fn != NULL) fn(arg);
  }
}

/* ------------------------------------------------------------------ */
/* 派发                                                                */
/* ------------------------------------------------------------------ */

/* 已知号派发（由 claim 或向量/位图路径调用）。不跑 BH，也不动 in_irq。
 * 返回"应当恢复的上下文"：只有 tick（EX_TIMER 槽）会替换它（do_schedule 可能切线程）。 */
interrupt_context_t *irq_dispatch_ctx(u32 irq, interrupt_context_t *ic) {
  struct irq_desc *d;
  struct irq_action *a;
  int handled = 0;

  if (!irq_valid(irq)) {
    kprintf("irq: bogus irq %u\n", irq);
    return ic;
  }

  d = &desc[irq];

  /* 【开机可观测性】每个号第一次真正被派发时打印一行。串口上没有这行，就无法判断
   * 框架到底有没有在跑（老路径里 interrupt_get_source() 的 "irq dma %d" 之类的
   * 打印随老路径一起退场了）。只打一次，不进热路径。 */
  if (!d->seen) {
    d->seen = 1;
    kprintf("irq: first dispatch %u '%s'\n", irq,
            (irq == tick_irq) ? "tick" : irq_get_name(irq));
  }

  if (irq == tick_irq) {
    /* 【tick 为什么走老槽】do_schedule 需要 interrupt_context_t（可能切上下文），
     * perf 也通过覆盖 exception_handlers[EX_TIMER] 来挂钩；这里保持既有机制不变。
     * ic->no 也照老路径置成 EX_TIMER（handler 与 dump 都依赖它）。 */
    if (exception_handlers[EX_TIMER] != NULL && ic != NULL) {
      interrupt_context_t *saved_no_ic = ic;
      ic->no = EX_TIMER;
      ic = exception_handlers[EX_TIMER](saved_no_ic);
      if (ic == NULL) ic = saved_no_ic;
    }
    d->count++;
  } else if (d->action == NULL) {
    /* ★ 未注册：计数 + 一次性告警 + 自动 mask（IRQ 103 那类风暴的正解） */
    d->unhandled++;
    irq_warn_once(irq);
    if (!(d->flags & IRQ_FLAG_NO_AUTO_MASK)) {
      chip_call_mask(d, irq); /* 注意：这是"没人处理"的兜底，不是正常路径 */
    }
  } else {
    for (a = d->action; a != NULL; a = a->next) {
      if (a->fn != NULL && a->fn(irq, a->arg)) {
        handled = 1;
        break;
      }
    }
    d->count++;
    if (!handled) {
      /* 有 handler 但都返回 0（"不是我的"）：计数 + 一次性告警。
       * 这里**不**自动 mask —— 设备可能只是暂时没有自己的状态位。 */
      d->unhandled++;
      irq_warn_once(irq);
    }
  }

  /* ★ EOI 由框架唯一负责：handler 忘了 ack 也不会把低优先级中断饿死。
   * 有些控制器在 claim 时就算 ack 过了（IRQ_CHIP_EOI_AT_CLAIM），这时不再 eoi。 */
  if (d->chip != NULL && d->chip->eoi != NULL &&
      !(d->chip->flags & IRQ_CHIP_EOI_AT_CLAIM)) {
    d->chip->eoi(irq);
  }
  return ic;
}

/* 位图型控制器的遍历回调：把可能被替换的上下文带回给调用者 */
struct irq_pending_walk {
  interrupt_context_t *ic;
};

static int irq_pending_cb(u32 irq, void *arg) {
  struct irq_pending_walk *w = (struct irq_pending_walk *)arg;
  w->ic = irq_dispatch_ctx(irq, w->ic);
  return 0;
}

/* claim 型控制器入口（GIC/PLIC）：取号 → 派发 → EOI → BH。
 * ic 与返回值都是为了让 tick 的上下文切换能一路传回 exception_process()。 */
interrupt_context_t *irq_claim_and_dispatch(interrupt_context_t *ic) {
  struct irq_chip *c = primary_chip();
  u32 irq;

  if (c == NULL) return ic;

  in_irq++;

  if (c->get_active != NULL) {
    irq = c->get_active();
    if (irq >= 1020) {
      /* GIC：1023 = spurious，1022 = active-but-not-pending */
      if (irq_valid(irq)) desc[irq].spurious++;
      if (c->eoi != NULL && !(c->flags & IRQ_CHIP_EOI_AT_CLAIM)) c->eoi(irq);
    } else {
      ic = irq_dispatch_ctx(irq, ic);
    }
  } else if (c->for_each_pending != NULL) {
    /* 位图型控制器（BCM legacy/PL190/VIC）：让控制器把所有 pending 号喂进来 */
    struct irq_pending_walk w;
    w.ic = ic;
    c->for_each_pending(irq_pending_cb, &w);
    ic = w.ic;
  }

  irq_bh_poll();
  in_irq--;
  return ic;
}

/* 已知号入口（向量型：x86 IDT/Cortex-M NVIC；也供级联控制器在 handler 里调用）。
 * 走这条路的平台没有"tick 换上下文"的需求（向量型不注册 EX_TIMER 槽）。 */
void irq_dispatch(u32 irq) {
  in_irq++;
  irq_dispatch_ctx(irq, NULL);
  irq_bh_poll();
  in_irq--;
}
