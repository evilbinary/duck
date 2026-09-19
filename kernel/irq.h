/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
//
// 统一中断（IRQ）核心
//
// 设计文档：docs/develop/architecture/中断子系统设计.md
//
// 角色划分：
//   · 本文件/irq.c        中断核心（desc 表、注册、派发、EOI、统计、bottom-half）
//   · archcommon/irq_chip.h 中断控制器抽象（GICv2/GICv3/PLIC/PL190/8259…）
//   · platform/<soc>/      只做 irq_chip_register() + irq_set_tick() + IRQ 号宏
//   · 驱动                 irq_register() / irq_enable() / irq_request_bh()
//
// 【约定（驱动必读）】
//   1. handler 签名 int (*)(u32 irq, void *arg)：返回 1 = "是我的中断且已处理"，
//      返回 0 = "不是我的"（共享中断时框架会继续问下一个）。
//   2. handler 必须**先清设备自己的中断状态（W1C）**，再排队重活；否则电平中断会重复触发。
//   3. handler 里不要用 log_*（可能走 vfs，非中断安全），需要打印用 irq_warn_once()/kprintf。
//   4. 重活（memcpy、刷 cache、重启 DMA、组包）用 irq_request_bh() 推迟到退出中断前执行。
//   5. 未注册/没人认领的中断：计数 + 一次性告警 + **自动 mask**（可用
//      IRQ_FLAG_NO_AUTO_MASK 关掉），避免"未知中断风暴"把串口和 CPU 吃满。
//
#ifndef __IRQ_H__
#define __IRQ_H__

#include "arch/cpu.h" /* u32 等基础类型；cpu_cli/cpu_sti；interrupt_context_t */
#include "archcommon/irq_chip.h"

/* 每个平台的 IRQ 号上限（描述符表大小）。GIC 满配 1020，但单个 SoC 一般远小于此；
 * 平台可在 platform/<soc>/irq.h 里覆盖。 */
#ifndef IRQ_NUM_MAX
#define IRQ_NUM_MAX 256
#endif

#define IRQ_CHIP_MAX 4   /* 多控制器/级联（如 versatilepb PIC+SIC） */
#define IRQ_ACTION_MAX 64 /* 静态 action 池：核心不依赖内存分配器 */
#define IRQ_BH_MAX 16

/* irq_register 的标志 */
#define IRQ_FLAG_SHARED (1u << 0)       /* 允许同号多 handler */
#define IRQ_FLAG_NO_AUTO_MASK (1u << 1) /* 未处理时不要自动 mask（调试用） */
#define IRQ_FLAG_CASCADE (1u << 2)      /* 级联控制器：handler 内允许再次 dispatch */

typedef int (*irq_handler_t)(u32 irq, void *arg);

struct irq_action {
  irq_handler_t fn;
  void *arg;
  const char *name;
  struct irq_action *next;
};

struct irq_desc {
  struct irq_action *action; /* 共享中断链 */
  struct irq_chip *chip;     /* 该号所属控制器 */
  u32 count;                 /* 触发次数 */
  u32 unhandled;             /* 未注册/没人认领次数 */
  u32 spurious;              /* 伪中断次数 */
  u8 masked;
  u8 enabled;
  u8 logged;                 /* irq_warn_once 用 */
  u8 seen;                   /* 首次派发打印过没有（开机可观测性） */
  u8 flags;
};

/* ---------------- 注册与配置（多为线程上下文调用） ---------------- */

int irq_register(u32 irq, irq_handler_t fn, void *arg, const char *name, u32 flags);
int irq_unregister(u32 irq, irq_handler_t fn, void *arg);
int irq_enable(u32 irq);  /* 控制器 unmask */
int irq_disable(u32 irq); /* 控制器 mask，保留注册 */
int irq_mask(u32 irq);
int irq_unmask(u32 irq);
int irq_set_priority(u32 irq, u32 prio); /* 语义透传（各控制器量纲不同） */
int irq_set_type(u32 irq, u32 type);     /* IRQ_TYPE_* */
int irq_set_affinity(u32 irq, u32 cpu);  /* 单核平台返回 -1 */

/* ---------------- 查询与诊断 ---------------- */

u32 irq_get_count(u32 irq);
const char *irq_get_name(u32 irq);
int irq_in_interrupt(void);
void irq_warn_once(u32 irq);
void irq_dump(void); /* 打印整张表（线程上下文） */

/* ---------------- bottom half ---------------- */

typedef void (*irq_bh_fn)(void *arg);
/* 中断里排队；队列满时**就地执行**并计数 bh_overflow（与"直接在 ISR 里做"等价） */
int irq_request_bh(irq_bh_fn fn, void *arg);
/* 退出中断前调用（exception_process 里） */
void irq_bh_poll(void);

/* ---------------- 供 arch 入口 / 平台调用 ---------------- */

/* 中断状态的"保存/还原"（不是无条件开关）：返回进入时的中断状态，restore 按原值还原。
 * 用于"改表/初始化"这类临界区 —— 不能在原本关中断的上下文里无条件开中断，
 * 否则会提前放行 tick（见设计文档 §11.1-2 的启动风暴）。 */
u32 irq_save_flags(void);
void irq_restore_flags(u32 flags);

int irq_core_enabled(void); /* 未注册任何 chip ⇒ 0，平台走老的 interrupt_get_source() */
/* claim 型（GIC/PLIC）：内部取号 → 派发 → EOI → 跑 bottom half。
 * 【必须返回值透传】tick 走 EX_TIMER 槽，而 do_schedule() 可能返回**切换后的新上下文**
 * （context switch），丢掉它就等于丢掉时钟抢占 ⇒ 返回值必须一路传回 exception_process()。 */
interrupt_context_t *irq_claim_and_dispatch(interrupt_context_t *ic);
/* 已知号（向量型 x86/NVIC、位图型回调）走这个，返回值同上 */
interrupt_context_t *irq_dispatch_ctx(u32 irq, interrupt_context_t *ic);
void irq_dispatch(u32 irq); /* = 包装：dispatch + bh_poll（无 ic，忽略返回值） */

struct irq_desc *irq_get_desc(u32 irq);

#endif
