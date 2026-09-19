/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
//
// 中断控制器抽象（irq_chip）
//
// 目的：把"怎么取中断号 / 怎么 EOI / 怎么屏蔽"从各平台 init.c 的 if/else 链里抽出来，
// 变成 runtime 注册的对象（duck/kernel/irq.c 里的 irq_chip_register()）。
// 这样驱动只认 IRQ 号（platform/<soc>/irq.h 里的 IRQ_*），不再改平台文件。
//
// 【跨平台必须容纳的差异】见仓库里实际存在的控制器：
//   · claim 型（读 IAR/claim 拿号）：GICv2（v3s/t113/cubieboard2…）、PLIC
//   · 位图型（只给 pending 位）：BCM2835/37 legacy intc、PL190 PIC+SIC、Allwinner VIC
//   · 向量型（硬件直接选 handler，号=向量）：x86 IDT、Cortex-M NVIC、ESP32
//   · 特殊情况：raspi5 的注释明确"读 GICC_IAR 即 ack"，所以要能声明 EOI_AT_CLAIM
//   · 没有 EOI 的（NVIC/ESP32 硬件自动）：eoi 填 NULL
//   · 多控制器级联（versatilepb PIC+SIC）：按号段注册多个 chip
//   · 纯 tick 平台（riscv-virt/esp32/stm32f4xx/dmulator…）：不注册 chip，走老路径
//
// 因此：下列所有回调**都允许为 NULL**，框架调用前一律判空。
//
#ifndef ARCHCOMMON_IRQ_CHIP_H
#define ARCHCOMMON_IRQ_CHIP_H

#include "types.h"

/* 触发类型 */
#define IRQ_TYPE_NONE 0
#define IRQ_TYPE_LEVEL_HIGH 1
#define IRQ_TYPE_LEVEL_LOW 2
#define IRQ_TYPE_EDGE_RISING 3
#define IRQ_TYPE_EDGE_FALLING 4

/* chip 能力标志 */
#define IRQ_CHIP_EOI_AT_CLAIM (1u << 0) /* get_active() 本身完成了 ack（raspi5 型） */

struct irq_chip {
  const char *name;

  /* —— 取中断号（三种模型按平台填其一）—— */
  u32 (*get_active)(void); /* claim 型：GIC 读 GICC_IAR；PLIC claim */
  int (*for_each_pending)(
      int (*cb)(u32 irq, void *arg),
      void *arg); /* 位图型：BCM/PL190/VIC；cb 返回非 0 提前停止 */

  /* —— 确认/结束/屏蔽（可空）—— */
  void (*ack)(u32 irq);
  void (*eoi)(u32 irq); /* v3s 变体用 gic_irqack2（GICC_DIR 未映射）；NVIC 型填 NULL */
  void (*mask)(u32 irq);
  void (*unmask)(u32 irq);

  /* —— 配置（可空）—— */
  void (*set_priority)(u32 irq, u32 prio); /* 语义透传：GIC=0 最高，NVIC=数值小最高 */
  void (*set_type)(u32 irq, u32 type);
  void (*set_affinity)(u32 irq, u32 cpu); /* 单核平台留 NULL */
  void (*send_ipi)(u32 cpu, u32 irq);     /* 单核平台留 NULL */

  u32 flags;
};

/* 平台在 boot 早期调用（必须早于任何使用该控制器的中断）。
 * irq_base/irq_num 用于把"全局 IRQ 号"映射到具体控制器（多控制器/级联）。 */
int irq_chip_register(struct irq_chip *chip, u32 irq_base, u32 irq_num);

/* 声明"哪个号是系统 tick"。框架在派发到这个号时，走老的 EX_TIMER 槽
 * （保持 perf 覆盖、do_schedule 需要 interrupt_context_t 等既有行为不变）。
 * 不调用则完全沿用平台 interrupt_get_source() 老路径。 */
void irq_set_tick(u32 irq);

#endif
