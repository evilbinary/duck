/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef GIC3_H
#define GIC3_H

#include "types.h"

// GICv3 API (ARMv8-A AArch64)
//
// Compatibility note:
// - We keep the legacy gic2-style function names so existing platform code
//   can switch headers with minimal changes.
// - For GICv3, gic_init_base(cpu_addr, dist_addr) means:
//     cpu_addr  -> GICR base (Redistributor frames)
//     dist_addr -> GICD base (Distributor)
//
// If you call gic_init(NULL), it falls back to typical QEMU "virt" defaults
// (GICD=0x08000000, GICR=0x080A0000) inside the implementation.

#ifdef __cplusplus
extern "C" {
#endif

void gic_init_base(void* gicr_base, void* gicd_base);
void gic_init(void* gicd_base_or_null);

void gic_enable(int cpu, int irq);
void gic_irq_priority(u32 cpu, u32 irq, u32 priority);

// Returns INTID (decoded from ICC_IAR1_EL1).
u32 gic_irqwho(void);

// Accepts either INTID (recommended, from gic_irqwho()) or raw IAR value.
void gic_irqack(int irq);

void gic_send_sgi(int cpu, int irq);

// Optional helpers (mainly for debug / legacy compatibility).
void gic_unpend(int irq);
void gic_check(void);
void gic_handler(u32 irq);
void gic_poll(u32 irq);

/* 统一中断框架（duck/kernel/irq.c）用的补充接口：
 * mask/unmask/触发类型/亲和性 + 控制器实例。
 * 实现见 libs/libarchcommon/arm/gic2.c；v3s 的变体（eoi=gic_irqack2）见 platform/v3s/gic.c。 */
void gic_irq_enable(int irq);
void gic_irq_disable(int irq);
void gic_irq_unmask(int irq);
void gic_irq_set_type(u32 irq, u32 type);
void gic_irq_set_affinity(u32 irq, u32 cpu);
u32 gic_get_active(void);
struct irq_chip;
extern struct irq_chip gicv2_chip;

#ifdef __cplusplus
}
#endif

#endif

