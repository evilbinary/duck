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
  asm volatile("mrc p15, 4, %0, c15, c0, 0" : "=r"(val));
  val >>= 15;
  val <<= 15;
  return (void *)val;
}

void gic_init(void *base) {
  if (base == 0) {
    base = gic_get_base();
  }
  gic_init_base(base + GICC_OFFSET, base + GICD_OFFSET);

  gic_dist_t *dp = gic.dist;
  gic_cpu_t *cp = gic.cpu;

  kprintf("GIC base  = %x dp =%x cp =%x\n", base, dp, cp);

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

  // enable
  dp->ctl = G0_ENABLE;
  cp->pm = 0xff;
  cp->bp = 0x7;
  cp->ctl = G0_ENABLE;

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

void gic_irq_priority(u32 cpu, u32 irq, u32 priority) {
  gic_dist_t *gp = gic.dist;
  // 优先级别设置
  gp->ipriority[irq] = priority;
}

void gic_unpend(int irq) {
  int x = irq / 32;
  unsigned long mask = 1 << (irq % 32);
  // GICD_ISPENDRn 寄存器，修改中断的pending状态
  gic.dist->icpend[x] = mask;
}

u32 gic_irqwho(void) { return gic.cpu->ia; }

void gic_irqack(int irq) {
  gic.cpu->eoi = irq;

  /* 【收尾要不要再写 GICC_DIR（GICC + 0x1000）—— 判据来自规范本身，不用平台去声明】
   * 依据 ARM IHI 0048B《GIC Architecture Specification V2.0》：
   *   · §3.2.1：GICv2 中 "when GICC_CTLR.EOImode is set to 0, a valid EOIR write
   *     **also deactivates** the interrupt it references" ⇒ EOImode=0 时 EOIR 一步
   *     到位（priority drop + 去激活），根本不需要 DIR；
   *   · Table 4-30 / 4-31 [9] EOImode(NS/S)：
   *       0 = "GICC_EOIR has both priority drop and deactivate interrupt functionality.
   *            **Accesses to the GICC_DIR are UNPREDICTABLE.**"
   *       1 = "GICC_EOIR has priority drop functionality only. The GICC_DIR register has
   *            deactivate interrupt functionality."（split 模式）
   * ⇒ 所以正确判据就是 EOImode 本身 = GICv2 的 GICC_CTLR **bit9**（注意不是 bit1，
   *   bit1 是 EnableGrp1/AckCtl），硬件已经把答案放在寄存器里了。
   *
   * 本仓库各平台 gic_init*() 写的都是 GICC_CTLR = G0_ENABLE(0x01) ⇒ EOImode=0 ⇒ 走第一
   * 档 ⇒ 这里不会写 DIR，与"EOImode=0 时写 DIR 的行为是 UNPREDICTABLE"正好一致。
   * 对 V3s 还顺带避开一个坑：DIR 在 GICC+0x1000 = 0x01c83000（GIC-400 TRM 表 3-1 给
   * CPU interface 的区间是 0x2000-0x3FFF，DIR 在其末页），而内核只 page_map 了
   * 0x01c81000 / 0x01c82000 两页（platform/v3s/init.c）⇒ 一旦去写就会在中断上下文里
   * data abort（实测：DMA 完成中断第一次到来时 "exception inside IRQ handler no=2"，
   * pc 停在下面那条 dir 写上，pte 1c83000 -> 0）。
   * 【将来改用 split 模式】把 GICC_CTLR bit9 置 1 时，必须同时把 DIR 那一页映射上
   * （V3s: page_map(0x01C83000, 0x01C83000, PAGE_DEV)），否则就会踩上面那个坑。 */
  if (gic.cpu->ctl & (1u << 9)) {
    gic.cpu->dir = irq;
  }
  gic_unpend(irq);
}

void gic_send_sgi(int cpu, int irq) {
  // 通过 GICD_SGIR Software Generated Interrupt Register软中断
  unsigned int mask = 1 << (cpu & 0xff);
  irq &= 0xf;  // in the range 0-15
  gic.dist->sgi = mask << 16 | irq;
}

void gic_check(void) {
  gic_dist_t *gp = gic.dist;
  kprintf("check GIC pending ispend:\n");
  for (int i = 0; i < 32; i++) {
    kprintf(" %d=%x", i, gp->ispend[i]);
  }
  kprintf("\n");
}

void gic_handler(u32 irq) {
  gic_dist_t *gp = gic.dist;
  gic_cpu_t *cp = gic.cpu;

  int irq1;
  irq1 = cp->ia;
  kprintf("irq=>%d\n", irq1);
  unsigned long mask = 1 << (irq % 32);

  int x = irq / 32;

  if (gp->ispend[x] & mask) {
    kprintf("GIC iack = %x\n", irq);

    // timer_handler(0);
    gic_irqack(irq);
  }
  // ms_delay ( 5 );
}

void gic_poll(u32 irq) {
  gic_dist_t *gp = gic.dist;
  gic_cpu_t *cp = gic.cpu;
  int x = irq / 32;

  for (;;) {
    // ms_delay ( 2000 );
    // kprintf("GIC pending: %x %x\n", gp->ispend[0], gp->ispend[1] );
    unsigned long mask = 1 << (irq % 32);
    if (gp->ispend[x] & mask) {
      gic_check();
      gic_handler(irq);
      kprintf("+GIC pending: %x\n", gp->ispend[x]);
    }
  }
}