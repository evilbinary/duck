/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "gic2.h"

gic_t gic;

void gic_init_base(void *cpu_addr, void *dist_addr) {
  gic.cpu = cpu_addr;
  gic.dist = dist_addr;
}

void *gic_get_base() {
  unsigned val;
  __asm__ volatile("mrc p15, 4, %0, c15, c0, 0;"
      : "=r" (val)
      );
  val >>= 15;
  val <<= 15;
  return (void *)val;
}

void gic_init(void *base) {
  if (base == 0) {
    base = gic_get_base();
    gic_init_base(base + GICC_OFFSET, base + GICD_OFFSET);
  }
  kprintf("GIC base  = %x\n", base);

  gic_dist_t *dp = gic.dist;
  gic_cpu_t *cp = gic.cpu;
  unsigned long *p;
  int i;

  p = (unsigned long *)&dp->itargets;
  // targets=0x1c81800
  kprintf("GIC target = %x\n", p);
  p = (unsigned long *)&dp->icfg;
  // confg=0x1c81c00
  kprintf("GIC config  = %x\n", p);
  p = (unsigned long *)&dp->sgi;
  // sgi=0x1c81f00
  kprintf("GIC soft = %x\n", p);

  // iid=0x1c81008
  kprintf("GIC iid = %x %x\n", &dp->iid, dp->iid);

  // init
  dp->ctl = 0;

  // clear
  for (i = 0; i < 32; ++i) {
    dp->icpend[i] = 0xffffffff;
  }
  for (i = 0; i < 8; ++i) {
    dp->igroup[i] = 0;
  }

  //enable
  // dp->ctl = G0_ENABLE;
  // cp->pm = 0xff;
  // cp->bp = 0x7;
  // cp->ctl = G0_ENABLE;

  kprintf("GIC init end\n");

}

void gic_irq_enable(int irq) {
  int x = irq / 32;
  unsigned long mask = 1 << (irq % 32);
  // 通过设置GICD_ICENABLERn寄存器，开启中断
  gic.dist->isenable[x] = mask;
}

void gic_enable(int cpu, int irq) {
  gic_dist_t *gp = gic.dist;
  gic_cpu_t *cp = gic.cpu;

  // 设置目标 cpu
  gp->itargets[irq] |= (1 << cpu) & 0xff;
  // 优先级别设置
  gp->ipriority[irq] = 0;

  // set security
  unsigned int mask = 1 << (irq & 0x1f);
  unsigned int reg = irq / 32;
  unsigned int value = gp->igroup[reg];
  value &= ~mask;
  gp->igroup[reg] = value;

  // irq开启
  gic_irq_enable(irq);

  gp->ctl = G0_ENABLE;
  cp->pm = 0xff;
  cp->bp = 0x7;
  cp->ctl = G0_ENABLE;

}

void gic_unpend(int irq) {
  int x = irq / 32;
  unsigned long mask = 1 << (irq % 32);
  // GICD_ISPENDRn 寄存器，修改中断的pending状态
  gic.dist->icpend[x] = mask;
}

int gic_irqwho(void) { return gic.cpu->ia; }

void gic_irqack(int irq) {
  gic.cpu->eoi = irq;
  gic_unpend(irq);
}

void gic_send_sgi(int cpu, int irq) {
  // 通过 GICD_SGIR Software Generated Interrupt Register软中断
  unsigned int mask = 1 << (cpu & 0xff);
  irq &= 0xf;  // in the range 0-15
  gic.dist->sgi = mask << 16;
}
