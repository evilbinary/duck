/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "cpu.h"

#include "context.h"
#include "kernel/memory.h" /* kmalloc/kfree/KERNEL_TYPE */
#include "kernel/string.h" /* kmemset */

extern boot_info_t* boot_info;
u32 cpus_id[MAX_CPU];

// 31:14-N -> va[29:20] va[19:12]
#define TTBCRN_4K 0b010
#define TTBCRN_16K 0b000
#define TTBCR_LPAE 1 << 31

/* data cache clean by MVA to PoC */
void dccmvac(unsigned long mva) {
  asm volatile("mcr p15, 0, %0, c7, c10, 1" : : "r"(mva) : "memory");
}

int cpu_pmu_version() {
  u32 pmu_id = 0;
  asm volatile("MRC p15, 0, %0, c9, c0, 0" : "=r"(pmu_id));
  // PMU 版本信息
  return (pmu_id >> 4) & 0xF;
}

void cpu_pmu_enable(int enable, u32 timer) {
  if (enable == 1) {
  } else if (enable == 0) {
  }
}

unsigned int cpu_cyclecount(void) {
  unsigned int value = 0;

  return value;
}

void cpu_icache_disable() { asm("mcr  p15, #0, r0, c7, c7, 0\n"); }

void cpu_invalid_tlb() {
  asm volatile("mcr p15, 0, %0, c8, c7, 0" : : "r"(0));  // unified tlb
  asm volatile("mcr p15, 0, %0, c8, c6, 0" : : "r"(0));  // data tlb
  asm volatile("mcr p15, 0, %0, c8, c5, 0" : : "r"(0));  // instruction tlb

  dsb();
  isb();
}

void cp15_invalidate_icache(void) {
  /* 【只失效 I-cache】ARM926 上 c7,c5,0 = 仅失效整个 I-cache。
   * 之前用的 c7,c7,0 会把 D-cache 一起失效 —— D-cache 关闭时无所谓，
   * 但一旦开启 D-cache，栈/堆的脏行会被直接丢弃 ⇒ 数据损坏。
   * 这里与 armv7 / armv7-a / armv8-a 的实现（c7,c5,0 / ic iallu）保持一致。
   * 需要在 D-cache 关闭状态下整片失效 I+D 的场景（如开 D-cache 前），
   * 请在调用点显式写 c7,c7,0。 */
  asm volatile(
      "mov r0, #0\n"
      "mcr p15, 0, r0, c7, c5, 0\n"  // Invalidate ICache only
      :
      :
      : "r0", "memory");
  dsb();
}

u32 read_dfar() {
  u32 val = 0;
  asm volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(val));
  return val;
}

u32 cpu_get_fault() { return read_dfar(); }

u32 read_dfsr() {
  u32 val = 0;
  asm volatile("mrc p15, 0, %0, c5, c0, 0" : "=r"(val));
  return val;
}

/* COW 判断：本次 data abort 是否为"写"访问（DFSR bit11 = WnR） */
int page_fault_is_write(void) { return (read_dfsr() & (1u << 11)) ? 1 : 0; }

u32 read_pc() {
  u32 val = 0;
  asm volatile("ldr %0,[r15]" : "=r"(val));
  return val;
}

u32 read_ifsr() {
  u32 val = 0;
  asm volatile("mrc p15, 0, %0, c5, c0, 1" : "=r"(val));
  return val;
}

u32 read_fp() {
  u32 val = 0;
  asm volatile("mov %0,fp" : "=r"(val));
  return val;
}

static inline u32 read_ttbcr(void) {
  u32 val = 0;
  asm volatile("mrc p15, 0, %0, c2, c0, 2" : "=r"(val));
  return val;
}

static inline void write_ttbcr(u32 val) {
  asm volatile("mcr p15, 0, %0, c2, c0, 2" : : "r"(val) : "memory");
}

void write_ttbr0(u32 val) {
  asm volatile("mcr p15, 0, %0, c2, c0, 0" : : "r"(val) : "memory");
}

static inline u32 read_ttbr0() {
  u32 val = 0;
  asm volatile("mrc p15, 0, %0, c2, c0, 0" : "=r"(val));
  return val;
}

static inline void write_ttbr1(u32 val) {
  asm volatile("mcr p15, 0, %0, c2, c0, 1" : : "r"(val) : "memory");
}

static inline u32 read_ttbr1() {
  u32 val = 0;
  asm volatile("mrc p15, 0, %0, c2, c0, 1" : "=r"(val));
  return val;
}

u32 cpu_set_domain(u32 val) {
  u32 old;
  asm volatile("mrc p15, 0, %0, c3, c0,0\n" : "=r"(old));
  asm volatile("mcr p15, 0, %0, c3, c0,0\n" : : "r"(val) : "memory");
  return old;
}

/* invalidate unified TLB by MVA and ASID */
void tlbimva(unsigned long mva) {
  asm volatile("mcr p15, 0, %0, c8, c7, 1" : : "r"(mva) : "memory");
}

void cpu_disable_l1_cache() {
  u32 reg;
  asm("mrc p15, 0, %0, c1, c0, 0" : "=r"(reg) : : "cc");  // SCTLR
  // Actually disable caches/buffers.
  reg &= ~(1 << 12);  // Instruction cache disable.
  reg &= ~(1 << 2);   // Data cache disable.
  reg &= ~(1 << 3);   // Write buffer disable.
  asm volatile("mcr p15, 0, %0, c1, c0, #0" : : "r"(reg) : "cc");  // SCTLR
  dsb();
  isb();
}

/* 【整片清 D-cache：逐字照抄 Linux proc-arm926.S 的
 * cpu_arm926_switch_mm / arm926_flush_kern_cache_all】
 * ARM926 没有"整片 clean"的 MCR 形式；它提供的是专有的 test,clean,invalidate
 * 指令 —— MRC 形式、opcode_2=3，把状态写进 APSR 的 NZCV（TRM 明确：读 c7 仅对
 * 两个 test-and-clean 操作有定义），Z 表示"没有更多脏行"，循环到清完为止。
 * 之后失效 I-cache、排空写缓冲。这就是 Linux 在 ARM926 上每次 switch_mm 都做
 * 的事，顺序一致：清缓存 → 失效 I → drain WB → 写 TTBR0 → 失效 TLB（见
 * context.c）。此前试 MCR c7,c14,0 之所以 UNDEF，是形式和 opcode 都不对。 */
void cpu_flush_dcache_all(void) {
  asm volatile(
      "mov r0, #0                             \n"
      "1:  mrc p15, 0, APSR_nzcv, c7, c14, 3  \n" /* test,clean,invalidate DCache */
      "    bne 1b                             \n"
      "    mcr p15, 0, r0, c7, c5, 0          \n" /* invalidate I cache */
      "    mcr p15, 0, r0, c7, c10, 4         \n" /* drain WB */
      :
      :
      : "r0", "memory", "cc");
  isb();
}

void cpu_set_page(u32 page_table) {
  kprintf("cpu_set_page\n");

  // dccmvac(page_table);
  // set ttbcr0
  write_ttbr0(page_table);
  isb();
  kprintf("cpu_set_page61\n");

  // write_ttbr1(page_table);

  kprintf("cpu_set_page62\n");
  isb();
  // write_ttbcr(TTBCRN_16K);

  kprintf("cpu_set_page7\n");
}

void cpu_disable_page() {
  // u32 reg;
  // asm("mcr     p15, #0, r0, c8, c7, #0\n");   // @ invalidate tlb
  // asm("mrc p15, 0, %0, c1, c0, 0" : "=r"(reg) : : "cc");
  // reg &= ~(1 << 0);
  // asm volatile("mcr p15, 0, %0, c1, c0, #0" : : "r"(reg) : "cc");
}

void cpu_enable_smp_mode() {
  // Enable SMP mode for CPU0
  // asm volatile(
  //   "mrc p15, 1, r0, R1, C15\n" // Read CPUECTLR.
  //   "orr r0, r0, #1 << 6 \n" // Set SMPEN.
  //   "mcr p15, 1, R0, R1, C15"); // Write CPUECTLR.

  // asm volatile(
  //     "mrc p15, 0, r0, c1, c0, 1\n"
  //     "orr r0, r0, #1 << 6\n"
  //     "mcr p15, 0, r0, c1, c0, 1\n");
}


inline void cpu_invalidate_tlbs(void) {
  asm("mcr p15, 0, r0, c8, c7, 0\n"
      "mcr p15,0,0,c7,c10,4\n"
      :
      :
      : "r0", "memory");
}

static inline u32 get_ccsidr(void) {
  u32 ccsidr;

  __asm__ __volatile__("mrc p15, 1, %0, c0, c0, 0" : "=r"(ccsidr));
  return ccsidr;
}

static inline u32 get_cache(void) {
  u32 cache;

  __asm__ __volatile__("mrc p15, 0, %0, c0, c0, 1" : "=r"(cache));
  return cache;
}

static inline void __v5_cache_inv_range(u32 start, u32 stop, u32 line) {
  u32 mva;

  start &= ~(line - 1);
  if (stop & (line - 1)) stop = (stop + line) & ~(line - 1);
  for (mva = start; mva < stop; mva = mva + line) {
    __asm__ __volatile__("mcr p15, 0, %0, c7, c6, 1" : : "r"(mva));
  }
}

static inline void __v5_cache_flush_range(u32 start, u32 stop, u32 line) {
  u32 mva;

  start &= ~(line - 1);
  if (stop & (line - 1)) stop = (stop + line) & ~(line - 1);
  for (mva = start; mva < stop; mva = mva + line) {
    __asm__ __volatile__("mcr p15, 0, %0, c7, c14, 1" : : "r"(mva));
  }
}

/*
 * Flush range(clean & invalidate), affects the range [start, stop - 1]
 */
void cpu_cache_flush_range(unsigned long start, unsigned long stop) {
  u32 cache;
  u32 line;

  cache = get_cache();
  line = 1 << ((cache & 0x3) + 3);
  __v5_cache_flush_range(start, stop, line);
  dsb();
}

/*
 * Invalidate range, affects the range [start, stop - 1]
 */
void cache_inv_range(unsigned long start, unsigned long stop) {
  u32 cache;
  u32 line;

  cache = get_cache();
  line = 1 << ((cache & 0x3) + 3);
  __v5_cache_inv_range(start, stop, line);
  dsb();
}

/* ---- 统一 cache 接口（armv5 强实现） ------------------------------------- */
void cpu_flush_dcache_range(unsigned long start, unsigned long stop) {
  cpu_cache_flush_range(start, stop);
}

void cpu_invalidate_dcache_range(unsigned long start, unsigned long stop) {
  cache_inv_range(start, stop);
}

void cpu_invalidate_icache(void) { cp15_invalidate_icache(); }

void cache_flush_range(unsigned long start, unsigned long stop) {
  u32 cache;
  u32 line;

  cache = get_cache();
  line = 1 << ((cache & 0x3) + 3);
  __v5_cache_flush_range(start, stop, line);
  dsb();
}

/* 【ARMv5 专属：页清零时的别名处理】实现 arch/cpu.h 的 cpu_zero_phy_page()。
 * ARM926 的 D-cache 是 VIVT —— 同一物理页在「恒等映射 PA」与「用户 VA」下是
 * 两个不同的 cache tag。页分配路径的语义是：先在恒等映射下把新页清零，随后
 * 内核再按用户 VA 往同一页写内容（加载器写 ELF / 用户栈内容）。
 * 若这里只 memset 不做 cache 维护，D-cache 里会留下一条 tag=PA 的零值行，与
 * 用户 VA 那一份分叉；等这条 PA 行被驱逐时会把用户页内容覆盖成 0，表现为用户
 * 进程执行到垃圾（实测三种落点：PREF ABORT 到 0x1000、UNDEF 跳到 0x81c、
 * /bin/ls 在 0x304ffbc8 data fault）。
 * 因此 memset 之后立刻 clean+invalidate：让物理内存成为唯一权威副本，后面
 * 用户 VA 侧再写就不会有第二条"影子数据"。调用方（kernel/memory.c 的
 * valloc / memory_stack_ensure）只表达"把这一页清零"的意图，细节留在本层。 */
void cpu_zero_phy_page(void* p, unsigned long size) {
  if (p == NULL || size == 0) {
    return;
  }
  kmemset(p, 0, size);
  cpu_flush_dcache_range((unsigned long)p, (unsigned long)p + size);
}

void cpu_enable_page() {
  kprintf("cpu_enable_page\n");

  cpu_enable_smp_mode();
  kprintf("cpu_enable_page1\n");

  u32 reg;

  /* 【开 D-cache 之前先整片失效 I+D】此刻 C=0，CPU 写不进 cache，失效不会
   * 丢脏数据，只会丢掉 bootloader(u-boot/fel) 可能残留在 cache 里的行。
   * 必须在写 SCTLR 之前做：一旦 C=1，任何整体失效都可能丢内核脏行。 */
  asm volatile("mov r0, #0\n"
               "mcr p15, 0, r0, c7, c7, 0\n"
               :
               :
               : "r0", "memory");
  dsb();

  // read mmu
  asm("mrc p15, 0, %0, c1, c0, 0" : "=r"(reg) : : "cc");  // SCTLR
  reg |= 0x1;                                             // M enable mmu
  // reg |= 1 << 1;  // Alignment check enable.

  /* D-cache / write buffer：
   *   - 页表属性现在是【写通 WT】（armv5/mm.h 的 PAGE_KERNEL/PAGE_KMEM/
   *     PAGE_USER/PAGE_FB = L2_ATTR_WT = L2_CNB，C=1 B=0）。之所以不用写回
   *     WB：ARM926 没有"整片 D-cache 的 clean+invalidate"操作，切地址空间时
   *     只能"只失效"，因此必须保证缓存里没有脏行 —— 详见 mm.h 的说明。
   *   - armv5/mm.c 的 page_create / page_copy / page_map_on 都对本架构做了
   *     dccmvac clean（页表对 MMU 遍历器可见），具备开 D-cache 的条件。
   * 出问题（数据/外设异常）时可把 ARMV5_DCACHE 置 0，快速回退到"仅 I-cache"。 */
#ifndef ARMV5_DCACHE
/* 【开启：做法逐字照抄 Linux proc-arm926.S，不再是猜测】
 *
 * 背景：ARM926 是 VIVT 缓存，本内核各进程共用同一套用户 VA（EXEC 0x60000000 /
 * STACK 0x70000000 / HEAP 0x70100000+），各自映射到不同物理页；切地址空间必须
 * 整片清缓存，否则上一个地址空间的缓存行会被新进程当成自己的数据/指令命中
 * （实测：D-cache 开着时 `ls` 一跑，shell(tid 1) 恢复执行就把栈里的值当返回
 * 地址，pc 跳进自己的栈）。
 *
 * 此前三次失败的根因都已清楚：
 *   1) 用了 MCR c7,c14,0 —— ARM926 的"整片 clean+invalidate"不是这个形式；
 *   2) WT 的 C/B 位写反（1<<2 实为 C=0,B=1，即类设备内存）；
 *   3) 没有对照参考实现。
 * Linux 的 cpu_arm926_switch_mm 给出标准答案（已逐字照抄，见本文件
 * cpu_flush_dcache_all 与 context.c）：
 *   - WB 模式：MRC p15,0,APSR_nzcv,c7,c14,3（test,clean,invalidate）循环到
 *     Z=1，再失效 I-cache、drain WB，然后才写 TTBR0、失效 TLB；
 *   - 映射属性保持写回(WB)，与 Linux 一致（__arm926_proc_info 即
 *     BUFFERABLE|CACHEABLE）。
 * 出问题（数据/外设异常）时把这里置 0 可快速回退到"仅 I-cache"。 */
#define ARMV5_DCACHE 1
#endif
#if ARMV5_DCACHE
  reg |= 1 << 2;   // Data cache enable.
  reg |= 1 << 3;   // Write buffer enable.
#else
  reg &= ~(1 << 2);  // Data cache disable.
  reg &= ~(1 << 3);  // Write buffer disable.
#endif

  reg |= 1 << 8;  // System protection bit.
  reg |= 1 << 9;  // ROM protection bit.
  /* SW：允许用户态执行 swp/swpb。ARMv5 无 LDREX/STREX，musl 的原子 CAS
   * （cpu_cmpxchg）和 libatomic 都用 swp；SW=0 时用户态 swp 直接 UNDEF。 */
  reg |= 1 << 10;
  // reg|= 1<<23; //0 = VMSAv4/v5 and VMSAv6, subpages enabled 1 = VMSAv6,
  // subpages disabled.

  reg |= 1 << 12;  // Instruction cache enable.

  // reg |= 1 << 13;  // vic low 0  vic hight 1
  asm volatile("mcr p15, 0, %0, c1, c0, #0" : : "r"(reg) : "cc");  // SCTLR
  dsb();
  isb();

  kprintf("cpu_enable_page2\n");

  // 只失效 I-cache（cp15_invalidate_icache 现在是 c7,c5,0）：
  // 开 D-cache 后绝不能再整片失效 I+D，否则丢栈/堆脏行。
  cp15_invalidate_icache();
  cpu_invalid_tlb();

  /* 【缓存状态回读】把刚写进 SCTLR 的位读回来，确认真的生效：
   * ARMv5/ARM926 上开启后应为 M=1 C=1 W=1 I=1（ARMV5_DCACHE=0 时 C/W 为 0）。
   * 若期望值不符，说明 SCTLR 写未生效或被人改动。 */
  {
    u32 sctlr, ctr;
    asm volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr) : : "cc");
    asm volatile("mrc p15, 0, %0, c0, c0, 1" : "=r"(ctr));
    kprintf("SCTLR=%x M=%d C(dcache)=%d W=%d I(icache)=%d Z=%d B=%d TRE=%d\n",
            sctlr, sctlr & 1u, (sctlr >> 2) & 1u, (sctlr >> 3) & 1u,
            (sctlr >> 12) & 1u, (sctlr >> 11) & 1u, (sctlr >> 7) & 1u,
            (sctlr >> 28) & 1u);
    kprintf("CTR=%x\n", ctr);
  }

  kprintf("cpu_enable_page3\n");
}

/* ================= 缓存效果自测（ARMv5，诊断用） =================
 * ARM926EJ-S 没有 PMU（cpu_cyclecount 恒 0），只能用调度 tick 计时
 * （SCHEDULE_FREQUENCY=1000 ⇒ 1 tick = 1ms）。
 * 读 4KB（可常驻 D-cache）与 256KB（远超 D-cache）各一遍，比较 KB/s：
 *   - D-cache 关闭（C=0）时两者应接近 —— 都是直接打 DRAM；
 *   - 打开 D-cache 后 4KB 应明显快于 256KB，这两个数就是"是否真走
 *     cache"的判据（与 armv7-a 的 cpu_mem_bw_test 同理）。
 * 由 kernel.c 在 -DMM_DEBUG_PROBE=1 时调用。 */
#ifndef SCHEDULE_FREQUENCY
#define SCHEDULE_FREQUENCY 1000 /* 1 tick = 1ms */
#endif
extern u32 schedule_get_ticks(void);

static u32 cache_read_probe(u32 size, u32 iters, u32* out_ms) {
  u32* buf = (u32*)kmalloc(size, KERNEL_TYPE);
  volatile u32 sum = 0;
  u32 n, i, k, t0, dt, kb;

  if (buf == NULL) {
    *out_ms = 0;
    return 0;
  }
  n = size / 4;
  for (i = 0; i < n; i++) buf[i] = i;
  cpu_flush_dcache_range((unsigned long)buf, (unsigned long)buf + size);

  t0 = schedule_get_ticks();
  for (k = 0; k < iters; k++) {
    for (i = 0; i < n; i++) sum += buf[i];
  }
  dt = schedule_get_ticks() - t0;
  if (dt == 0) dt = 1;
  kb = (size / 1024) * iters; /* 总读 KB */
  kfree(buf);
  *out_ms = dt;
  (void)sum;
  /* kb*1000/dt 在 4KBx2000 / 256KBx32 下都远小于 2^32，不用 64 位除法 */
  return (kb * SCHEDULE_FREQUENCY) / dt;
}

void cpu_cache_selftest(void) {
  u32 sctlr, ms, small, big;

  asm volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr) : : "cc");

  kprintf("==== CACHE selftest (ARMv5/ARM926) ====\n");
  kprintf("SCTLR=%x M=%d C(dcache)=%d W=%d I(icache)=%d\n", sctlr, sctlr & 1u,
          (sctlr >> 2) & 1u, (sctlr >> 3) & 1u, (sctlr >> 12) & 1u);

  small = cache_read_probe(4 * 1024, 2000, &ms);
  kprintf("READ 4KB   x2000: %d KB/s (%d ms)  [4KB fits D-cache]\n", small, ms);
  big = cache_read_probe(256 * 1024, 32, &ms);
  kprintf("READ 256KB x32  : %d KB/s (%d ms)  [256KB >> D-cache]\n", big, ms);

  if (sctlr & (1u << 2)) {
    kprintf("CACHE selftest: D-cache=ON -> 4KB should be much faster\n");
  } else {
    kprintf("CACHE selftest: D-cache=OFF(C=0) -> both ~equal(DRAM bound); "
            "I-cache=%d\n",
            (sctlr >> 12) & 1u);
  }
  kprintf("==== CACHE selftest end ====\n");
}

void cpu_init(int cpu) {
  // cpu_enable_smp_mode();
  // cpu_enable_ca7_smp();
  for (int i = 0; i < MAX_CPU; i++) {
    cpus_id[i] = i;
  }
}

void cpu_halt() {
  for (;;) {
  };
}

void cpu_wait() {}

ulong cpu_get_cs(void) {
  ulong result;

  return result;
}

int cpu_tas(volatile int* addr, int newval) {
  /* 【必须返回"旧值"】acquire() 的写法是 while (cpu_tas(lock,1)==1)。
   * 原实现直接 return newval(=1) ⇒ 条件恒真 ⇒ 任何 acquire() 死循环。
   * 之前没暴露是因为启动路径不碰 acquire；system() 走 pipe → rw_queue_read_wait
   * → acquire 才第一次命中，表现为读 pipe 时父进程在 acquire 里空转卡死。
   * ARM926EJ-S(ARMv5) 没有 LDREX/STREX，但有 SWP：原子交换并返回旧值。
   * 单核 + 内核态不可抢占，SWP 足够。 */
  int result;
  asm volatile("swp %0, %1, [%2]"
               : "=&r"(result)
               : "r"(newval), "r"(addr)
               : "memory");
  return result;
}

void cpu_backtrace(void* tfp, void** buf, int size) {
  int topfp = tfp;  // read_fp();
  for (int i = 0; i < size; i++) {
    u32 fp = *(((u32*)topfp) - 3);
    u32 sp = *(((u32*)topfp) - 2);
    u32 lr = *(((u32*)topfp) - 1);
    u32 pc = *(((u32*)topfp) - 0);
    if (i == 0) {
      *buf++ = pc;
    }  // top frame
    if (fp != 0) {
      *buf++ = lr;
      // kprintf(" %x\n", lr);
    }  // middle frame
    else {
      kprintf("bottom frame %x\n", pc);
    }  // bottom frame, lr invalid
    if (fp == 0) break;
    topfp = fp;
  }
}

int cpu_get_number() { return boot_info->tss_number; }

u32 cpu_get_id() {
  int cpu = 0;
  return cpu & 0xf;
}

u32 cpu_get_index(int idx) {
  if (idx < 0 || idx > cpu_get_number()) {
    kprintf("out of bound get cpu idx\n");
    return 0;
  }
  return cpus_id[idx];
}

int cpu_init_id(u32 id) {
  // kprintf("cpu init id %d\n", id);
  ipi_enable(id);
  return 0;
}

int cpu_start_id(u32 id, u32 entry) {
  // start at  at entry-point on boot init.c
  // kprintf("cpu start id %d entry: %x\n", id,entry);
  lcpu_send_start(id, entry);
  return 0;
}

u32 read_cntv_tval(void) {
  u32 val;
  asm volatile("mrc p15, 0, %0, c14, c3, 0" : "=r"(val));
  return val;
}

void write_cntv_tval(u32 val) {
  asm volatile("mcr p15, 0, %0, c14, c3, 0" ::"r"(val));
  return;
}

u32 read_cntfrq(void) {
  u32 val;
  asm volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(val));
  return val;
}

void enable_cntv(u32 cntv_ctl) {
  asm volatile("mcr p15, 0, %0, c14, c3, 1" ::"r"(cntv_ctl));  // write CNTV_CTL
}

void disable_cntv(u32 cntv_ctl) {
  asm volatile("mcr p15, 0, %0, c14, c3, 1" ::"r"(cntv_ctl));  // write CNTV_CTL
}

/*do fast 64bit div constant
 *	because 52 bit timer can provide 23 years cycle loop
 * 	we can do 64bit divided as below：
 * 	x / 6 = x / (4096/682) = x * 682 / 4096 = (x * 682) >> 12
 *   it will save hundreds of cpu cycle
 */
static __inline uint64_t fast_div64_6(uint64_t x) { return (x * 682) >> 12; }

static inline uint64_t timer_read_sys_usec(void) {  // read microsec
  return fast_div64_6(read_cntvct());
}

void cpu_delay_usec(uint64_t count) {
  // uint64_t s = timer_read_sys_usec();
  // uint64_t t = s + count;
  // while (s < t) {
  //   s = timer_read_sys_usec();
  // }
}

void cpu_delay_msec(u32 count) { cpu_delay_usec(count * 1000); }

void cpu_delay(int n) {
  // cpu_delay_msec(n);
  while (n > 0) {
    n--;
  }
}

void cpu_cli() {
  u32 val;
  asm("mrs %[v], cpsr" : [v] "=r"(val)::);
  val |= 0x80;
  asm("msr cpsr_cxsf, %[v]" : : [v] "r"(val) :);
}

void cpu_sti() {
  u32 val;
  asm("mrs %[v], cpsr" : [v] "=r"(val)::);
  val &= ~0x80;
  asm("msr cpsr_cxsf, %[v]" : : [v] "r"(val) :);
}

/* 【ARMv5 缺失的原子/屏障帮助函数】GCC 在 ARMv5（无 ldrex/strex）下无法内联
 * __sync_* 内建，会生成对下列帮助函数的调用；而本工具链的 libgcc 里没有它们、
 * 且 f1c200s/v3s 的链接带 -nostdlib ⇒ 链接期报
 *   undefined reference to `__sync_lock_test_and_set_4'
 *   undefined reference to `__sync_synchronize'
 * ARMv5 为单核，用"关 IRQ 临界区"即可满足语义；屏障用 CP15 写缓冲排空。 */
int __sync_lock_test_and_set_4(volatile void* ptr, int val) {
  unsigned int cpsr;
  int old;
  __asm__ __volatile__("mrs %0, cpsr" : "=r"(cpsr));
  __asm__ __volatile__("msr cpsr_c, %0" : : "r"(cpsr | 0x80)); /* 关 IRQ */
  old = *(volatile int*)ptr;
  *(volatile int*)ptr = val;
  __asm__ __volatile__("msr cpsr_c, %0" : : "r"(cpsr)); /* 还原 */
  return old;
}

void __sync_synchronize(void) {
  __asm__ __volatile__("mcr p15, 0, %0, c7, c10, 4" : : "r"(0) : "memory");
}

void cpu_cmpxchg(void* ptr, u32 old_value, u32 new_value) {
  // asm(".word 0xf57ff05f\n" /* dmb sy                */
  //     ".word 0xe1923f9f\n" /* ldrex r3, [r2]        */
  //     ".word 0xe0530000\n" /* subs r0, r3, r0       */
  //     ".word 0x01820f91\n" /* strexeq r0, r1, [r2]  */
  //     ".word 0xf57ff05f\n" /* dmb sy                */
  //     ".word 0xe12fff1e\n" /* bx lr                 */
  // );
}