/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "archcommon/gic3.h"

// ARM GICv3: MMIO for Distributor/Redistributor, system registers for CPU interface.
// Keep the legacy gic2-style API (gic_init/gic_enable/gic_irqwho/gic_irqack/gic_send_sgi).

typedef struct {
  volatile u8* gicd;      // Distributor base
  volatile u8* gicr;      // Redistributor base (start of RD frames)
  volatile u8* gicr_rd;   // Current CPU RD frame base
  volatile u8* gicr_sgi;  // Current CPU SGI/PPI frame base
  u32 last_iar1;          // Raw ICC_IAR1_EL1 value of last ack
  u32 last_intid;         // INTID decoded from last_iar1
} gic3_state_t;

static gic3_state_t gic3;

// QEMU "virt" default mapping (when caller passes base==0).
#ifndef GICV3_GICD_BASE_DEFAULT
#define GICV3_GICD_BASE_DEFAULT 0x08000000UL
#endif
#ifndef GICV3_GICR_BASE_DEFAULT
#define GICV3_GICR_BASE_DEFAULT 0x080A0000UL
#endif

#define GICD_CTLR         0x0000
#define GICD_TYPER        0x0004
#define GICD_IGROUPR(n)   (0x0080 + 4 * (n))
#define GICD_ISENABLER(n) (0x0100 + 4 * (n))
#define GICD_ICENABLER(n) (0x0180 + 4 * (n))
#define GICD_IPRIORITYR   0x0400
#define GICD_IROUTER(n)   (0x6100 + 8 * ((n) - 32))

#define GICR_CTLR         0x0000
#define GICR_IIDR         0x0004
#define GICR_TYPER        0x0008
#define GICR_WAKER        0x0014
#define GICR_WAKER_ProcessorSleep (1U << 1)
#define GICR_WAKER_ChildrenAsleep (1U << 2)
#define GICR_SGI_OFFSET   0x10000

#define GICR_IGROUPR0     0x0080
#define GICR_ISENABLER0   0x0100
#define GICR_ICENABLER0   0x0180
#define GICR_ICPENDR0     0x0280
#define GICR_IPRIORITYR   0x0400

static inline void dsb_sy(void) { __asm__ volatile("dsb sy" : : : "memory"); }
static inline void isb(void) { __asm__ volatile("isb" : : : "memory"); }

static inline u32 mmio_read32(volatile u8* base, u32 off) {
  return *(volatile u32*)(base + off);
}
static inline void mmio_write32(volatile u8* base, u32 off, u32 v) {
  *(volatile u32*)(base + off) = v;
}
static inline u64 mmio_read64(volatile u8* base, u32 off) {
  return *(volatile u64*)(base + off);
}
static inline void mmio_write64(volatile u8* base, u32 off, u64 v) {
  *(volatile u64*)(base + off) = v;
}

static inline u64 read_mpidr_el1(void) {
  u64 v;
  __asm__ volatile("mrs %0, mpidr_el1" : "=r"(v));
  return v;
}

static inline void write_icc_sre_el1(u64 v) { __asm__ volatile("msr icc_sre_el1, %0" : : "r"(v)); }
static inline void write_icc_pmr_el1(u64 v) { __asm__ volatile("msr icc_pmr_el1, %0" : : "r"(v)); }
static inline void write_icc_bpr1_el1(u64 v) { __asm__ volatile("msr icc_bpr1_el1, %0" : : "r"(v)); }
static inline void write_icc_ctlr_el1(u64 v) { __asm__ volatile("msr icc_ctlr_el1, %0" : : "r"(v)); }
static inline void write_icc_igrpen1_el1(u64 v) { __asm__ volatile("msr icc_igrpen1_el1, %0" : : "r"(v)); }
static inline u32 read_icc_iar1_el1(void) {
  u64 v;
  __asm__ volatile("mrs %0, icc_iar1_el1" : "=r"(v));
  return (u32)v;
}
static inline void write_icc_eoir1_el1(u32 v) { __asm__ volatile("msr icc_eoir1_el1, %0" : : "r"((u64)v)); }
static inline void write_icc_dir_el1(u32 v) { __asm__ volatile("msr icc_dir_el1, %0" : : "r"((u64)v)); }
static inline void write_icc_sgi1r_el1(u64 v) { __asm__ volatile("msr icc_sgi1r_el1, %0" : : "r"(v)); }

static inline u32 mpidr_to_aff(u64 mpidr) {
  u32 aff0 = (u32)(mpidr & 0xFF);
  u32 aff1 = (u32)((mpidr >> 8) & 0xFF);
  u32 aff2 = (u32)((mpidr >> 16) & 0xFF);
  u32 aff3 = (u32)((mpidr >> 32) & 0xFF);
  return (aff3 << 24) | (aff2 << 16) | (aff1 << 8) | aff0;
}

static void gic3_pick_redistributor(void) {
  if (gic3.gicr == NULL) {
    gic3.gicr_rd = NULL;
    gic3.gicr_sgi = NULL;
    return;
  }
  u32 want_aff = mpidr_to_aff(read_mpidr_el1());
  volatile u8* rd = gic3.gicr;
  for (;;) {
    u64 typer = mmio_read64(rd, GICR_TYPER);
    u32 aff = (u32)(typer >> 32);
    if (aff == want_aff) {
      gic3.gicr_rd = rd;
      gic3.gicr_sgi = rd + GICR_SGI_OFFSET;
      return;
    }
    if (typer & (1UL << 4)) {  // Last
      break;
    }
    rd += 0x20000;  // 128KB per redistributor (RD + SGI/PPI)
  }
  gic3.gicr_rd = (volatile u8*)gic3.gicr;
  gic3.gicr_sgi = (volatile u8*)gic3.gicr + GICR_SGI_OFFSET;
}

static void gic3_redist_wake(void) {
  if (gic3.gicr_rd == NULL) return;
  u32 w = mmio_read32(gic3.gicr_rd, GICR_WAKER);
  w &= ~GICR_WAKER_ProcessorSleep;
  mmio_write32(gic3.gicr_rd, GICR_WAKER, w);
  dsb_sy();
  while (mmio_read32(gic3.gicr_rd, GICR_WAKER) & GICR_WAKER_ChildrenAsleep) {
  }
}

static void gic3_cpuif_init(void) {
  // Enable system register interface, disable IRQ/FIQ bypass.
  write_icc_sre_el1((1U << 0) | (1U << 3) | (1U << 4));
  isb();
  write_icc_pmr_el1(0xFF);
  write_icc_bpr1_el1(0);
  write_icc_ctlr_el1(0);
  isb();
  write_icc_igrpen1_el1(1);
  isb();
}

void gic_init_base(void* cpu_addr, void* dist_addr) {
  // For GICv3 we interpret cpu_addr as GICR base, dist_addr as GICD base.
  gic3.gicr = (volatile u8*)cpu_addr;
  gic3.gicd = (volatile u8*)dist_addr;
}

void gic_init(void* base) {
  if (base == NULL) {
    gic_init_base((void*)GICV3_GICR_BASE_DEFAULT, (void*)GICV3_GICD_BASE_DEFAULT);
  } else {
    // If caller only provides one base, treat it as GICD and assume QEMU-like offset for GICR.
    // Prefer calling gic_init_base(gicr, gicd) for real hardware.
    gic_init_base((void*)((u64)base + (GICV3_GICR_BASE_DEFAULT - GICV3_GICD_BASE_DEFAULT)), base);
  }

  gic3_pick_redistributor();
  gic3_redist_wake();

  if (gic3.gicd == NULL || gic3.gicr_sgi == NULL) {
    kprintf("GICv3 init failed: gicd=%lx gicr=%lx\n", (u64)gic3.gicd, (u64)gic3.gicr);
    return;
  }

  // Disable distributor while programming.
  mmio_write32(gic3.gicd, GICD_CTLR, 0);
  dsb_sy();
  while (mmio_read32(gic3.gicd, GICD_CTLR) & (1U << 31)) {
  }

  // Put all SPIs into Group1NS by default.
  u32 typer = mmio_read32(gic3.gicd, GICD_TYPER);
  u32 lines = (typer & 0x1F) + 1;  // number of 32-interrupt blocks
  for (u32 n = 1; n < lines; n++) {
    mmio_write32(gic3.gicd, GICD_IGROUPR(n), 0xFFFFFFFF);
  }
  dsb_sy();

  // Enable affinity routing + Group1NS at distributor.
  // GICD_CTLR: EnableGrp1NS (bit1), ARE_NS (bit4).
  u32 ctlr = (1U << 1) | (1U << 4);
  mmio_write32(gic3.gicd, GICD_CTLR, ctlr);
  dsb_sy();
  while (mmio_read32(gic3.gicd, GICD_CTLR) & (1U << 31)) {
  }

  // Make SGI/PPI Group1NS by default.
  mmio_write32(gic3.gicr_sgi, GICR_IGROUPR0, 0xFFFFFFFF);
  dsb_sy();

  gic3_cpuif_init();

  kprintf("GICv3 init: gicd=%lx gicr=%lx rd=%lx iid=%x typer=%x\n",
          (u64)gic3.gicd, (u64)gic3.gicr, (u64)gic3.gicr_rd,
          mmio_read32(gic3.gicr_rd, GICR_IIDR), typer);
}

static void gic3_irq_set_priority(u32 irq, u8 prio) {
  if (irq < 32) {
    *(volatile u8*)(gic3.gicr_sgi + GICR_IPRIORITYR + irq) = prio;
  } else {
    *(volatile u8*)(gic3.gicd + GICD_IPRIORITYR + irq) = prio;
  }
}

static void gic3_irq_enable(u32 irq) {
  if (irq < 32) {
    mmio_write32(gic3.gicr_sgi, GICR_ISENABLER0, 1U << (irq & 31));
  } else {
    mmio_write32(gic3.gicd, GICD_ISENABLER(irq / 32), 1U << (irq & 31));
  }
  dsb_sy();
}

void gic_enable(int cpu, int irq) {
  if (gic3.gicd == NULL || gic3.gicr_sgi == NULL) {
    gic_init(NULL);
  }

  if (irq < 0) return;
  u32 intid = (u32)irq;

  // Route SPIs to a CPU. (SGI/PPI are always per-CPU and ignore IROUTER.)
  if (intid >= 32) {
    u64 route = (u64)(cpu & 0xFF);  // Aff0 = cpu, others 0
    mmio_write64(gic3.gicd, GICD_IROUTER(intid), route);
  }

  gic3_irq_set_priority(intid, 0x80);
  gic3_irq_enable(intid);
}

void gic_irq_priority(u32 cpu, u32 irq, u32 priority) {
  (void)cpu;
  gic3_irq_set_priority(irq, (u8)priority);
}

u32 gic_irqwho(void) {
  u32 iar = read_icc_iar1_el1();
  u32 intid = (iar & 0x3FF);
  gic3.last_iar1 = iar;
  gic3.last_intid = intid;
  return intid;
}

void gic_irqack(int irq) {
  u32 v = (u32)irq;
  if (v == gic3.last_intid) {
    v = gic3.last_iar1;
  }
  write_icc_eoir1_el1(v);
  write_icc_dir_el1(v);
  isb();
}

void gic_send_sgi(int cpu, int irq) {
  // SGI intid range 0..15 (group1).
  u32 intid = (u32)(irq & 0xF);
  u64 sgi =
      ((u64)intid << 24) |          // INTID
      ((u64)1U << (cpu & 0xF));     // TargetList (Aff0)
  write_icc_sgi1r_el1(sgi);
  dsb_sy();
}

// Legacy debug helpers (no-op / minimal for GICv3)
void gic_unpend(int irq) {
  u32 intid = (u32)irq;
  if (intid < 32) {
    mmio_write32(gic3.gicr_sgi, GICR_ICPENDR0, 1U << (intid & 31));
  } else {
    mmio_write32(gic3.gicd, 0x0280 + 4 * (intid / 32), 1U << (intid & 31));  // GICD_ICPENDR
  }
}

void gic_check(void) {
  // Intentionally minimal for GICv3.
}

void gic_handler(u32 irq) {
  (void)irq;
}

void gic_poll(u32 irq) {
  (void)irq;
  for (;;) {
  }
}