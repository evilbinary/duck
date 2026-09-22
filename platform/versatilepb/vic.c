/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
/* VersatilePB 中断控制器：PL190 PIC + SIC（级联）。
 *
 * 拓扑（见 QEMU hw/arm/versatilepb.c）：
 *   · SIC @0x10003000 的 32 个源，默认全部汇总到 PIC 的 bit31
 *     （vpb_sic_update 把 level&mask 打到 parent[31]）。
 *   · AACI=sic[24]、KMI0(键盘)=sic[3]、KMI1(鼠标)=sic[4]。
 *   · 其余外设（timer0=pic[4]、uart=pic[12..14]、lcd=pic[16]…）直连 PIC。
 * 号段：PIC=0..31，SIC=32..63。PIC 的 for_each_pending 在 bit31 上展开 SIC 位图。
 */
#include "gpio.h"
#include "kernel/kernel.h"
#include "kernel/irq.h"
#include "libs/include/archcommon/irq_chip.h"

static int pic_for_each_pending(int (*cb)(u32 irq, void *arg), void *arg) {
  u32 pend = io_read32(PIC_BASE + PIC_STATUS);
  u32 n;
  for (n = 0; n < 32; n++) {
    if (!(pend & (1u << n))) {
      continue;
    }
    if (n == IRQ_PIC_SIC) {
      u32 s = io_read32(SIC_BASE + SIC_STATUS);
      u32 m;
      for (m = 0; m < 32; m++) {
        if (s & (1u << m)) {
          if (cb(IRQ_SIC_BASE + m, arg)) {
            return 1;
          }
        }
      }
    } else {
      if (cb(n, arg)) {
        return 1;
      }
    }
  }
  return 0;
}

static void pic_eoi(u32 irq) { io_write32(PIC_BASE + PIC_VECT_ADDR, 0); }

static void pic_mask(u32 irq) {
  io_write32(PIC_BASE + PIC_INT_CLEAR, 1u << irq);
}

static void pic_unmask(u32 irq) {
  io_write32(PIC_BASE + PIC_INT_ENABLE, 1u << irq);
}

static void sic_mask(u32 irq) {
  io_write32(SIC_BASE + SIC_INT_ENCLR, 1u << (irq - IRQ_SIC_BASE));
}

static void sic_unmask(u32 irq) {
  io_write32(SIC_BASE + SIC_INT_ENABLE, 1u << (irq - IRQ_SIC_BASE));
}

static struct irq_chip versatile_pic_chip = {
    .name = "versatile-pic",
    .get_active = NULL,
    .for_each_pending = pic_for_each_pending,
    .eoi = pic_eoi,
    .mask = pic_mask,
    .unmask = pic_unmask,
};

static struct irq_chip versatile_sic_chip = {
    .name = "versatile-sic",
    .get_active = NULL,
    .for_each_pending = NULL,
    .eoi = NULL,
    .mask = sic_mask,
    .unmask = sic_unmask,
};

void versatile_irq_chip_init(void) {
  irq_chip_register(&versatile_pic_chip, 0, 32);
  irq_chip_register(&versatile_sic_chip, IRQ_SIC_BASE, 32);
  irq_set_tick(IRQ_PIC_TIMER0);
}
