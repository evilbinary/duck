#include "arch/arch.h"
#include "gpio.h"
#include "kernel/page.h"
#include "libs/include/types.h"
#include "libs/include/archcommon/gic2.h" /* 通用 GICv2 驱动：gicv2_chip/gic_init_base（GIC-400） */
#include "libs/include/archcommon/irq_chip.h" /* 统一中断框架：struct irq_chip/irq_chip_register/irq_set_tick */
#include "kernel/irq.h"

static void io_write32(volatile unsigned int* port, u32 data);
static u32 io_read32(volatile unsigned int* port);
void ipi_clear(int cpu);

static void io_write32(volatile unsigned int* port, u32 data) {
  *port = data;
}

static u32 io_read32(volatile unsigned int* port) {
  return *port;
}

static void io_write64(volatile u64 addr, u32 data) {
  *(volatile unsigned int*)addr = data;
}

static u32 io_read64(volatile u64 addr) {
  return *(volatile unsigned int*)addr;
}

// AP entry stub in boot-armv8-a.s (firmware spin-table target).
// Weak so non single-kernel builds still link (then SMP release is disabled).
extern char apu_entry __attribute__((weak));
/* 【实验】主核发给从核的 MMU 配置（boot-armv8-a.s 的 .data，64B 对齐）：
 * {ttbr0_el1, mair_el1, tcr_el1}，放行前写好并清缓存，之后不再重写。 */
extern volatile u64 ap_ttbr[3];

static u64 cntfrq[MAX_CPU] = {0};
/* 和 raspi3 一样：从核先进来等着，mp_init 再放行。
 * 否则从核会在 GIC 分发器打开前就 timer_init，PPI27 写不进去，tick 永远不来。
 * 主核此时 D-cache 已开，从核还关着，标志必须刷到内存。 */
static volatile u32 ap_release[MAX_CPU];

static void delay_cycles(int n) {
  for (volatile int i = 0; i < n; i++) {
  }
}

/* 【控制台初始化】真机 DTB 权威结论：
 *   /chosen stdout-path = serial10
 *   /soc@107c000000/serial@7d001000  ⇒ CPU 侧 = 0x107D001000（UART10_* 宏）
 * 固件（config.txt enable_uart=1 + overlay）已把引脚复用/上下拉/波特率分频
 * 全部配好，BL31 就是在这条口上打印的（实测 boot 阶段打 "AAA" 证实）。
 * ⇒ Pi5 这里【只做幂等重设】：LCRH=8N1+FIFO、CR=UARTEN|TXE|RXE；
 *    不动 IBRD/FBRD（波特率保持固件的值），
 *    【一个 legacy GPIO 寄存器都不碰】——GPFSEL1 / GPPUD / GPPUDCLK0 /
 *    GPIO_PUP_PDN_CNTRL_REG0 是 BCM2711 的，Pi5 上不存在（RP1 是另一套
 *    IO_BANK0/PADS），读写它们会拿 UR → 同步外部中止（boot 阶段最初的死因）。 */
static void uart_init(void) {
#ifdef RASPI5
  io_write32(UART10_LCRH, (3u << 5) | (1u << 4)); /* 8N1 + FIFO enable */
  io_write32(UART10_CR, 0x301);                   /* UARTEN|TXE|RXE */
  return;
#endif
  // 以下为 raspi2/3 的 legacy BCM2837 路径（GPIO14/15 -> ALT0 + 分频）
  u32 r = io_read32(GPFSEL1);
  r &= ~((7u << 12) | (7u << 15));
  r |= (4u << 12) | (4u << 15);
  io_write32(GPFSEL1, r);

  // Pi 5: no GPPUD/GPPUDCLK. Pin 14/15 pull control lives in
  // GPIO_PUP_PDN_CNTRL_REG0 bits [29:28]/[31:30], 00 = no pull.
  u32 pup = io_read32(GPIO_PUP_PDN_CNTRL_REG0);
  pup &= ~((3u << 28) | (3u << 30));
  io_write32(GPIO_PUP_PDN_CNTRL_REG0, pup);
  delay_cycles(1500);

  io_write32(UART0_ICR, 0x7FF);  // clear pending UART interrupts

  // 50MHz UARTCLK -> 115200 baud: bauddiv = 50e6/(16*115200) = 27.1267
  io_write32(UART0_CR, 0);
  io_write32(UART0_IBRD, 27);
  io_write32(UART0_FBRD, 8);
  io_write32(UART0_LCRH, (3u << 5) | (1u << 4));  // 8N1, FIFO enable

  io_write32(UART0_IMSC, 0);  // mask all UART interrupts (polling)

  // Enable UART, TX and RX.
  io_write32(UART0_CR, (1u << 0) | (1u << 8) | (1u << 9));

  // Drain any stale RX data.
  while ((io_read32(UART0_FR) & (1u << 4)) == 0) {
    (void)io_read32(UART0_DR);
  }
}

/* 【极早期探针 · 第四轮：把"复位次数"当进度条】覆盖 boot/arm64 的弱默认实现
 * （空函数），由 boot-armv8-a.s 在最前面调用（MMU 未开 → 直接物理地址访问）。
 *
 * 【真机已验证（上一版）】把 PSCI SYSTEM_RESET 放在第一条指令、且此前只碰
 * RAM，实测固件日志确实自己重来了一次，且标记跨热复位活了下来（只多一次
 * 重启）。于是三件事确定：
 *   1) 固件确实跳到我们的 0x80000 ✓（排除"根本没跑到"）；
 *   2) smc #0 且 x0=0x84000009 能复位整机 ✓ → 有不依赖任何外设的输出通道；
 *   3) RAM 标记跨热复位保留 ✓ → 可做"跨复位状态机"。
 *
 * 【本轮做法】"写设备"一旦被同步外部中止打死就彻底静默、什么也看不出，所以
 * 把危险操作拆成一步一步：每次启动只做一步，做完把 step+1 写回 RAM 并复位。
 * 哪一步把 CPU 打死，重启就停在那一步。用户只需数"固件日志重来了几次"
 * （每步约 7~11 秒，看第一行 "RP1_UART ..." 出现几次）：
 *   0 次            → 第 0 步（写 0x7D001000）就中止了
 *   N 次后静默      → 第 N 步（0 起数）中止
 *   7 次后无限循环  → 六步全部走通（default 分支的"报告结束"标记）
 * 同时每一步都会往对应串口写字母：哪一步的地址正好是控制台，就会在那一刻
 * 看到 BBBBBB / AAAAAA / FFFFFF / HHHHHH / GGGGGG。
 */
/* 读一个 32 位设备寄存器（用于"设备到底答不答应"的判定：PL011 的
 * PeriphID0 @+0xFE0 应为 0x11，见 DTB `arm,primecell-periphid = <0x341011>`）。
 * 注意：对没有设备应答的 PCIe 地址做【读】可能拿到 UR → 同步外部中止 → CPU
 * 卡死，所以读测试一律放在最后一步（卡死就正好说明"没设备应答"）。 */
static u32 dev_read32(u64 addr) { return *(volatile u32*)(uintptr_t)addr; }
static void dev_write32(u64 addr, u32 v) {
  *(volatile u32*)(uintptr_t)addr = v;
  dsb();
}

/* 三个候选控制台地址（DTB /tmp/rpi5b.dts 推出来的 CPU 侧地址） */
#define UART10_LO 0x7D001000ULL      /* SoC UART10 的 legacy 低外设镜像 */
#define UART10_HI 0x107D001000ULL    /* SoC UART10 原生高地址视图 */
#define RP1_UART0_ADDR 0x1F00030000ULL /* RP1 UART0：非预取窗口 + PCIe 0x30000 */

/* 只做"使能"：8N1+FIFO、UARTEN|TXE|RXE。分频（IBRD/FBRD）不动，保留固件设好
 * 的值 —— 那样波特率仍然是对的。
 * 【为什么必须写 CR/LCRH】上一轮只写 DR、五个地址全无反应；若固件在交接前把
 * TX 关了（或根本没使能这条 UART），只写 DR 就是往 FIFO 丢字节而不发送 ——
 * 看起来和"地址不对"一模一样。 */
static void pl011_enable(u64 base) {
  volatile u32* u = (volatile u32*)(uintptr_t)base;
  u[0x2C / 4] = 0x70;  /* LCRH: 8N1 + FIFO enable */
  u[0x30 / 4] = 0x301; /* CR: UARTEN|TXE|RXE */
  dsb();
}

/* 使能 + 连写 6 个字符（字符之间留点时间，让 posted 写有节奏地出去）。 */
static void pl011_try(u64 base, char c) {
  volatile u32* u = (volatile u32*)(uintptr_t)base;
  pl011_enable(base);
  for (int i = 0; i < 6; i++) {
    u[0] = (u32)c;
    for (volatile int d = 0; d < 4000; d++) {
    }
  }
}

/* 结束标记用：足够长的忙等（不依赖任何定时器 —— 万一 CNTVCT 没跑，用计数器
 * 反而会永远等下去）。让"结束循环"看起来明显比"步骤/报告"慢很多。 */
static void slow_delay(void) {
  for (volatile u64 i = 0; i < 2000000000ULL; i++) {
  }
}

/* 【RC 寄存器读写】根复合体基址 0x1000120000（DTB pcie@1000120000
 * reg = <0x10 0x120000 0x00 0x9310>）。偏移与位域取自 Linux
 * drivers/pci/controller/pcie-brcmstb.c：
 *   0x4008 MISC_CTRL : [12]SCB_ACCESS_EN [13]CFG_READ_UR_MODE
 *                      [21:20]MAX_BURST_SIZE（BCM7712/2712 用 2 = 512B）
 *                      [10]RCB_MPS_MODE [7]RCB_64B_MODE
 *   0x4068 PCIE_STATUS: [7]PORT(1=RC) [6]LINK_IN_L23 [5]DL_ACTIVE
 *                      [4]PHYLINKUP
 *   0x9000 EXT_CFG_INDEX / 0x8000 EXT_CFG_DATA：配置空间间接访问
 * brcm_pcie_link_up() 判定 = DL_ACTIVE && PHYLINKUP。 */
#define RC_BASE 0x1000120000ULL
#define RC_MISC_CTRL 0x4008u
#define RC_PCIE_STATUS 0x4068u

static u32 rc_rd(u32 off) { return dev_read32(RC_BASE + off); }
static void rc_wr(u32 off, u32 v) {
  *(volatile u32*)(uintptr_t)(RC_BASE + off) = v;
  dsb();
}

/* 【PCIe 配置空间读】brcm_pcie_map_bus() 的用法：
 *   写 0x9000 = idx，其中 idx = (bus << 20) | (devfn << 12)
 *   然后读 0x8000 + (where & 0xFFF) 就是该寄存器的值
 * 我们在 MISC_CTRL 里置了 CFG_READ_UR_MODE：读不存在的设备返回 0xFFFFFFFF
 * 而不会中止 CPU —— 所以这一组读是安全的（不像内存读）。
 * 用它能直接问硬件：RP1（Pi 5 上在 bus 1 / devfn 0）的 vendor ID 与 BAR 被
 * 分到哪个 PCIe 地址 —— 这是"RP1 寄存器窗口到底在哪"最权威的答案。 */
static u32 rc_cfg_read(u32 bus, u32 devfn, u32 where) {
  rc_wr(0x9000, (bus << 20) | (devfn << 12));
  return rc_rd(0x8000 + (where & 0xFFFu));
}

/* 【RP1 的 GPIO/引脚复用】40 针排针上的引脚都在 RP1 里，寄存器区（DTB
 * gpio@d0000 的 reg 给出）：IO_BANK0 = 0xD0000、RIO = 0xE0000、
 * PADS_BANK0 = 0xF0000 —— 换算到 CPU 侧（RP1 窗口 0x1F00000000）就是
 * 0x1F0D0000 / 0x1F0E0000 / 0x1F0F0000。
 * 【纠正历史错误】最早那版探针写的是 0x1C0D0000（0x1C 窗口）—— 那是错的窗口
 * （0x1C 那个窗口映射的是 PCIe 0x4_00000000 起），所以那次"引脚重配"根本没
 * 落到 RP1 上，却让我们以为"引脚复用不是问题"。
 * 每脚 8 字节、CTRL 在 +4：GPIO14 → +0x74、GPIO15 → +0x7C；CTRL[4:0] = FUNCSEL。
 * PADS 每脚 4 字节：GPIO14 → +0x3C、GPIO15 → +0x40；0x40 = IN_ENABLE、无上下拉。 */
#define RP1_IO_BANK0 0x1F0D0000ULL
#define RP1_PADS_BANK0 0x1F0F0000ULL

/* 【RP1 时钟管理器】clocks@18000（RP1 内 0x40018000 → CPU 0x1F018000）。
 * 定义取自 Linux drivers/clk/clk-rp1.c（rpi-6.6.y）：
 *   CLK_UART_CTRL    = 0x00054  → CPU 0x1F018054
 *   CLK_UART_DIV_INT = 0x00058
 *   CLK_UART_SEL     = 0x00060（clk_uart 的 num_std_parents=0，父源在 AUXSRC）
 *   CLK_CTRL_ENABLE  = BIT(11) = 0x800
 * clk_uart 的父源顺序：0=pll_sys_pri_ph、1=pll_video、2=xosc、3..8=clksrc_gp0..5
 * 注意：DTB 的 assigned-clock-rates 里**没有** 0x0f（CLK_UART）——它是固件为
 * 自己的控制台打开的（enable_rp1_uart=1），Linux 侧由 PL011 驱动按波特率设。 */
#define RP1_CLK_UART_CTRL 0x1F018054ULL
#define RP1_CLK_UART_DIV_INT 0x1F018058ULL
#define RP1_CLK_CTRL_ENABLE 0x800u

/* 【RP1 UART 时钟分频扫描的候选值】单位：分频整数部分。
 * 0 = 不改动（保留固件留下的值，等价于"只置 ENABLE"）。
 * 依据：115200 = clk / (16 × (IBRD + FBRD/64))，而固件的 IBRD/FBRD 我们不动，
 * 所以只要把 clk 恢复成固件原来那个值，波特率就自动是对的 —— 而 clk =
 * 父时钟 / DIV_INT，父时钟未知，于是干脆把 DIV_INT 从 1 扫到 64。 */
static const u8 rp1_div_cands[] = {0, 1,  2,  3,  4,  5,  6,  8,  10, 12, 16,
                                   20, 24, 25, 26, 30, 32, 40, 48, 50, 60, 64};

static void uart_putc(volatile u32* base, char c);

/* 以十进制打出 1~3 位数（探测用，只走带边界的 uart_putc）。 */
static void put_dec(volatile u32* base, u32 v) {
  if (v >= 100u) {
    uart_putc(base, (char)('0' + (v / 100u) % 10u));
  }
  if (v >= 10u) {
    uart_putc(base, (char)('0' + (v / 10u) % 10u));
  }
  uart_putc(base, (char)('0' + v % 10u));
}

static void rp1_pinmux_uart0(u32 funcsel) {
  volatile u32* c14 = (volatile u32*)(uintptr_t)(RP1_IO_BANK0 + 0x74);
  volatile u32* c15 = (volatile u32*)(uintptr_t)(RP1_IO_BANK0 + 0x7C);
  volatile u32* p14 = (volatile u32*)(uintptr_t)(RP1_PADS_BANK0 + 0x3C);
  volatile u32* p15 = (volatile u32*)(uintptr_t)(RP1_PADS_BANK0 + 0x40);
  *c14 = (*c14 & ~0x1Fu) | (funcsel & 0x1Fu);
  *c15 = (*c15 & ~0x1Fu) | (funcsel & 0x1Fu);
  *p14 = 0x40;
  *p15 = 0x40;
  dsb();
}

/* 补上 Linux brcm_pcie_setup() 里 MISC_CTRL 的那一段。我们此前只写了出站
 * 窗口，从没设置过 SCB_ACCESS_EN / CFG_READ_UR_MODE / burst 等 —— 而 RP1
 * 的寄存器访问恰恰要求 RC 这一套齐备。 */
static void rc_misc_ctrl_init(void) {
  u32 v = rc_rd(RC_MISC_CTRL);
  v &= ~0x00303480u;                    /* 清这 5 个字段 */
  v |= 0x00003480u | (2u << 20);        /* 1<<12 |1<<13 |1<<10 |1<<7 | burst=2 */
  rc_wr(RC_MISC_CTRL, v);
}



/* 往一个候选 RP1 UART 地址做最小初始化并连写 6 个字符（实现见 pl011_try）。 */
static void rp1_uart_try(u64 base, char c) { pl011_try(base, c); }

/* 【PSCI 复位】DTB 有 `psci { method = "smc"; }`；x0 = 0x84000009 =
 * SYSTEM_RESET。真机已验证（上一版在第一条指令处复位成功，日志重启一次）。
 * 这是本板唯一不依赖任何外设的可观测通道，用它当"进度条"。 */
static void psci_reset(void) {
  __asm__ volatile(
      "mov w0, #9\n\t"
      "movk w0, #0x8400, lsl #16\n\t"
      "smc #0\n\t"
      "smc #0\n\t"
      "1:\n\t"
      "wfi\n\t"
      "b 1b\n\t" ::: "x0", "memory");
}

/* 【步进式探针 · 第十轮】每次启动只做一步，做完把 step+1 写回 RAM 再复位。
 * 跨复位状态：0x20000000 = {magic, step, payload}（热复位不丢，已实测）。
 *
 * 【前两轮真机结论】
 *   第八轮：内存读 0x1F00030000+0xFE0 成功返回 0x11 → 那个地址上有设备应答 ✓
 *   第九轮：FUNCSEL 1..8 八个值全试过、**一个字母都没出来**；状态回读的
 *           8 个 bit 也基本为 0（CR/LCRH/IBRD/FR 读回来是 0、GPIO CTRL 的
 *           FUNCSEL 读回来是 0）。
 *   → 综合起来最像：**设备在，但寄存器读回来是 0、写进去不生效** = 外设块
 *     时钟/电源没被使能（RP1 的 UART 时钟由 RP1 自己的 clock manager 提供，
 *     Linux 是在 clk-rp1.c 里打开 CLK_UART 的；我们没有那一步）。
 *
 * 【真机已验证（第十三轮前）】上一版的 payload 是 15（四位全中）= 写 RP1
 * 寄存器【生效】：IBRD 写-读回一致 ✓、GPIO14 CTRL 写-读回一致 ✓、
 * CR=0x301 写-读回一致 ✓。加上 PeriphID 能读回 0x11，所以"地址对、写有效"。
 * 那"能配置却发不出字"就只剩：**UART 没有波特率时钟**（字节进了 FIFO 但
 * 移位器不动）。另外：跨镜像的 RAM 残留会把旧 payload 重放（上一版踩到），
 * 所以这版 magic 带版本号（0x5A5A0013），且结束时【主动清状态】。
 *
 * 【本轮（第十四）核心思路：什么都不碰，只写 DR】
 * 由 Linux 源码确认的事实：
 *   · 地址对：pinctrl-rp1.c 的 PIN(14, pwm0,dpi,uart4,i2c3,uart0,gpio,...)
 *     → **位置 4 就是 uart0**，我们写的 funcsel=4 本来就是对的（不是它的问题）；
 *   · pads 的 bank0 偏移 = +0x0004、GPIO CTRL 步长 0x8（GPIO14 → +0x74）✓
 *     RP1_PAD_OUT_DISABLE 是 bit7，我们写 0x40 不会关输出 ✓
 *   · 前几轮已实测：写 IBRD/GPIO CTRL/CR 都**生效**。
 * 那唯一没试过的"最纯"组合就是：**除了数据寄存器 DR，其它寄存器一个都不写**
 * （不碰 funcsel、不碰 CR/LCRH/IBRD、不碰 pads）。固件从开机第一行就在用这
 * 条串口打印，说明交接瞬间它的状态本来是好的 —— 如果之前失败是我们自己把
 * 它改坏的，这一版就会立刻出字。
 *
 * 【第十四轮真机结论】config.txt 已经带上 enable_rp1_uart=1（日志里 uart0-pi5、
 * disable-bt-pi5 都加载了，kernel 481352 字节 = 那版），**"只写 DR、别的都不写"
 * 依然一个字都没有** → 说明不是我们改坏的，而是**固件在交接前把这条串口停车了**。
 * 【本轮（第十五）假设】停车用的是引脚控制里那两个我们一直"读-改-写保留"的字段：
 *   GPIO_CTRL bits12-13 OUTOVER：0=外设控制、1=反相、2=强制低、3=强制高
 *   GPIO_CTRL bits14-15 OEOVER ：0=外设控制、1..3=越权
 *   PADS bit7 OUT_DISABLE       ：1=输出关闭
 * 只要 OUTOVER/OEOVER 任一非 0（或 pad 输出被关），UART 配得再对，TX 也到不了
 * 引脚 —— 这和"寄存器读写全正常、却一个字节都出不来"完全吻合。
 *
 * 【第十六轮真机结论】只重启了 2 次就停住 = 第 2 次（写 16 字节那步）卡死。
 * 1（第 0 步写的 'F'）+ 16 = 17 > FIFO 的 16 格 → **写 DR 被永久阻塞** →
 * 既证明"我们的写确实进了 UART 的 FIFO"，也证明 **TX FIFO 永远排不出去**。
 * 同时重新审日志顺序："RP1_UART 0000001c00030000" 出现在 "Read /config.txt"
 * 【之前】，那时 enable_rp1_uart 还没生效 → 它是**默认控制台 = SoC UART10**
 * （官方 dts 注释："The system UART"，Pi5 左边 3 针调试口）打的。也就是说：
 * 适配器很可能接在**系统 UART**上，而前几轮我们一直在写 RP1 那条（没接线）！
 *
 * 步骤（每次启动推进 3 个候选，总共 22 个候选 → 8 轮，然后停住）：
 *   每一轮：关 UART（清发送 FIFO）→ 引脚 CTRL 写 4 + pad 0x40（解停车）
 *           → CLK_UART 置 ENABLE + 写本次候选的 DIV_INT → LCRH/CR 使能
 *           → 打出 "<分频>U\r\n"（DIV_INT=0 的候选打 "KU"）
 *   候选跑完：清状态 → 长停顿 → 永久停住（断电重上电即可重跑）
 *
 * 读数：找【看起来正常可读】的那一行，例如 "26U" → 分频 26 就复原了固件的
 * UART 时钟；"KU" 可读 → 说明"只置 ENABLE"就够（那就不是分频问题）。
 * 全是一堆乱码或什么都没有 → 时钟不是（唯一）原因，再往引脚/复位方向查。
 */
/* 【跨复位状态】三份冗余：主状态在 0x20000000，另有两个"我来过"的镜像标记。
 * 上一版只有一个位置，若被固件启动过程踩掉，就会永远从第 0 步重来 ——
 * 表现是"无限重启、永远不结束"。这里只要任意一份还在，就认为状态有效。
 *   st[0] = magic，st[1] = step，st[2] = payload */
#define PROBE_MAGIC 0x5A5A0022ULL

/* 【LED 报告通道】板上 ACT 灯 = dts 的 "2712_STAT_LED" = gpio@7d517c00 的第 9 脚。
 * gpio@7d517c00 是 BCM2712 自己的 GPIO 块（compatible "brcm,brcmstb-gpio"，
 * reg = <0x7d517c00 0x40>，CPU 侧 0x107D517C00）。寄存器布局取自 Linux
 * drivers/gpio/gpio-brcmstb.c：
 *   每 bank 8 个 32 位寄存器 = 0x20 字节；bank0 就在基址
 *   +0x00 ODEN，+0x04 DATA（输出值），+0x08 IODIR（方向），…
 * LED 是 active-low（dts flags = 0x01）→ 输出 0 = 亮。
 * 为什么用它：串口这条通道至今打不出一个字，而 LED 你能"眼睁睁看到"，
 * 不用数日志、不用重启计数 —— 闪烁次数就是一位数字。 */
#define LED_BASE 0x107D517C00ULL
#define LED_BIT 9u

static void led_wait(u32 n) {
  for (volatile u32 i = 0; i < n; i++) {
  }
}
static void led_set(int on) {
  volatile u32* g = (volatile u32*)(uintptr_t)LED_BASE;
  u32 d = g[0x04u / 4];
  if (on) {
    d &= ~(1u << LED_BIT);
  } else {
    d |= (1u << LED_BIT);
  }
  g[0x04u / 4] = d;
  dsb();
}
/* iodir_bit=0 / 1 两种方向极性各试一遍（bgpio 的 dirout 极性这里不猜）。 */
static void led_dir(u32 iodir_bit) {
  volatile u32* g = (volatile u32*)(uintptr_t)LED_BASE;
  u32 d = g[0x08u / 4];
  if (iodir_bit) {
    d |= (1u << LED_BIT);
  } else {
    d &= ~(1u << LED_BIT);
  }
  g[0x08u / 4] = d;
  dsb();
}
static void led_pulse(u32 on_units, u32 off_units) {
  led_set(1);
  led_wait(on_units);
  led_set(0);
  led_wait(off_units);
}

/* 【带边界的单字节输出】只在 TXFF(FR bit5) 为 0 时写入，最多等约 20 万次循环。
 * 真机教训（第十六轮）：UART 没有波特率时钟时字节排不出去，FIFO 满后
 * 对 DR 的写会【永远阻塞】，直接把 CPU 卡死 —— 那一轮就是这样停在 2 次重启。
 * 以后所有串口输出都必须走这个函数。 */
static void uart_putc(volatile u32* base, char c) {
  for (int t = 0; t < 200000; t++) {
    if ((base[0x18u / 4] & 0x20u) == 0u) {
      break;
    }
  }
  base[0] = (u32)c;
  dsb();
}
#define PROBE_STATE 0x20000000ULL
#define PROBE_MIRROR1 0x21000000ULL
#define PROBE_MIRROR2 0x22000000ULL

/* 只写数据寄存器 DR（偏移 0），别的寄存器一个都不碰。 */
static void dr_write(u64 base, char c, int n) {
  volatile u32* dr = (volatile u32*)(uintptr_t)base;
  for (int i = 0; i < n; i++) {
    *dr = (u32)c;
    for (volatile int d = 0; d < 4000; d++) {
    }
  }
  dsb();
}


void uart_send(u8 c) {
  /* 【LF 补 CR】内核文本只带 '\n'；终端对纯 LF 只下移不复位列 ⇒ 输出呈阶梯状
   * 缩进、难以阅读。这里遇 '\n' 先发一个 '\r'（递归一层；'\r' 不会再进这个
   * 分支）。boot 阶段自己写的就是 \n\r，走独立通路不受影响。 */
  if (c == '\n') {
    uart_send('\r');
  }
  /* 【已确认的控制台：SoC UART10 = serial10 = 0x107D001000】
   * 真机 DTB：/chosen stdout-path=serial10，/soc@107c000000/serial@7d001000
   * ⇒ CPU 侧 0x107D001000（旧代码漏了父节点 0x1_00000000 前缀，写进了 DRAM）。
   * 实测串口先打出 "AAA" 证实就是它。对它做【有界 FR 等待】+ 写 DR：
   * 16 字节 FIFO 不做流控会被连续写入冲爆（实测"前 ~30 字符正常、之后乱码"）；
   * 它是真实设备，读 FR 安全。上限 ~20 万次（无时钟时也不能卡死）。 */
  /* 【上限别太大】MMU 还没开时这次读是 Device-nGnRnE 强序访问，一次可能几百微秒；
   * 原来 20 万次 ⇒ 单个字符最坏 ~0.4 秒，21 字符的输出行要好几秒，看起来像卡死。
   * FIFO 排空一个字符只要 ~87µs（115200 波特），2000 次足够。 */
  for (int t = 0; t < 2000; t++) {
    if ((io_read32(UART10_FR) & 0x20) == 0) {
      break;
    }
  }
  io_write32(UART10_DR, c);
  /* 【只写 serial10】boot 阶段的自检已经证明：只有 serial10（0x107D001000）
   * 是活控制台（打出 "AAA"；RP1 两个候选窗口一个字母都没出）。
   * 以前往 0x1C00030000 / 0x1F00030000 兜底写的代码删掉了 —— 每个字符多两次
   * posted 总线写没有意义。若将来换线到 RP1 的 UART0（GPIO14/15），
   * 再按 gpio.h 的 RP1_UART0 说明加回来。 */
}

unsigned int uart_receive(void) {
  /* 只轮询【已确认】的 serial10（0x107D001000）。不再读 RP1 窗口的 UART0_FR：
   * 那里有没有设备还没定论，读错 PCIe 地址会拿 UR 把 CPU 打死。
   * FR 的 RXFE(bit4) 为 0 表示接收 FIFO 非空。 */
  for (;;) {
    if ((io_read32(UART10_FR) & 0x10) == 0) {
      return io_read32(UART10_DR) & 0xFF;
    }
  }
}

u32 read_core_timer_pending(int cpu) {
  // Pi 5 has no BCM2711 core irq source register.
  // NOTE: do NOT read GICC_IAR here - reading it acknowledges an interrupt
  // and would steal it from the normal dispatch path.
  return 0;
}

// Enable the GIC distributor once and the CPU interface per-core.
static void gic_cpu_init(int cpu) {
  /* 【复用通用 GICv2 驱动】raspi5 的控制器是 GIC-400（GICv2，见 gpio.h 的 DTB
   * 注释）。gic_init_base 只记录 GICC/GICD 基址 —— gicv2_chip 的取号/EOI/
   * mask/优先级都从这两个指针走；寄存器布局用 gic2.h 的结构体，不再手写。 */
  gic_init_base((void *)GICC_BASE, (void *)GICD_BASE);

  gic_dist_t *dp = (gic_dist_t *)GICD_BASE;
  gic_cpu_t *cp = (gic_cpu_t *)GICC_BASE;

  if (cpu == 0) {
    dp->ctl = G0_ENABLE;  // GICD_CTLR: enable distributor
  }
  cp->pm = 0xff;           // GICC_PMR: no priority masking
  cp->ctl = G0_ENABLE;     // GICC_CTLR: enable (EOImode=0)

  // Enable virtual timer PPI (banked in GICD_ISENABLER0 for SGI/PPI)
  dp->isenable[GIC_PPI_CNTVIRQ / 32] = 1u << (GIC_PPI_CNTVIRQ % 32);

  // SGI0 = IPI 收端。GICD_ISENABLER0 对 SGI/PPI 是 banked per-core —— 必须
  // 每个核在【自己的上下文】写一次才生效（mp_init 在主核代写对 AP 无效）。
  // 本函数由每核的 timer_init 调用 ⇒ 主核/AP 各自使能，AP 才收得到 IPI。
  dp->isenable[0] = 1u;

  /* Give PPI27 a reasonable priority（GICD_IPRIORITYR 每个中断 1 字节，
   * 而结构体把 0x400 建模成 u32[128] ⇒ 必须按字节索引，不能用 ipriority[27]） */
  ((volatile u8 *)dp->ipriority)[GIC_PPI_CNTVIRQ] = 0xA0;
}

void timer_init(int hz) {
  int cpu = cpu_get_id();
  kprintf("cpu %d timer init\n", cpu);

  /* 【Pi5 真机】CNTFRQ_EL0 是每核 system register，PSCI 拉起的 AP 那份
   * 没有被固件初始化（实测 AP 读出 0xffffdeff 这类垃圾，主核读出 54000000）。
   * 直接拿去算周期会得到 4.29M 的错误重装值 ⇒ CNTV 几十秒都不来一个点、
   * tick 永远不派发（表现为 AP 的 timer_ticks 恒 0）。按量级校验：非法时
   * 沿用主核在 cpu0 阶段算好的每 tick 值（主核 schedule_init 必先于 mp_init）。 */
  u64 frq_hz = read_cntfrq();
  if (frq_hz < 1000000u || frq_hz > 1000000000u) {
    cntfrq[cpu] = (cntfrq[0] != 0) ? cntfrq[0] : 1;
  } else {
    cntfrq[cpu] = frq_hz / hz;
  }
  if (cntfrq[cpu] == 0) {
    cntfrq[cpu] = 1;
  }
  if (cpu == 0) {
    kprintf("cntfrq %d\n", cntfrq[cpu]);
  }
  gic_cpu_init(cpu);

  /* 【接入统一中断框架 —— 与 raspi3/v3s 对齐，顺序有讲究】
   * 先 set_tick 再 register：反过来"chip 已注册、tick 还没声明"的窗口里若有中断
   * 进来，tick 号会被当"未注册号"自动 mask ⇒ 时钟永久停掉（v3s/raspi3 都记录了
   * 这条）。控制器实例直接用通用 GICv2 驱动的 gicv2_chip（取号 GICC_IAR / EOI
   * GICC_EOIR / mask / 优先级），不再在平台里手写；不注册的后果：primary_chip()
   * ==NULL ⇒ 中断既不取号也不 EOI ⇒ 第一次放开中断就 IRQ 风暴卡死（实测过）。
   * raspi5 没有 GICv3 的 GICR 帧，所以不能用 arm64/gic3.c，见 libarchcommon/ya.py。 */
  irq_set_tick(GIC_PPI_CNTVIRQ);
  irq_chip_register(&gicv2_chip, 0, 128);

  write_cntv_tval(cntfrq[cpu]);
  enable_cntv(1);
}

void timer_end(void) {
  int cpu = cpu_get_id();
  // Always reset timer to clear interrupt, avoids IRQ storm.
  if (cntfrq[cpu] != 0) {
    write_cntv_tval(cntfrq[cpu]);
  } else if (cntfrq[0] != 0) {
    write_cntv_tval(cntfrq[0]);
  } else {
    write_cntv_tval(1);
  }
}

extern void exception_vectors(void);

void platform_init(void) {
  /* 【异常向量必须尽早装上】否则任何 fault 都会跳到陈旧/垃圾向量 ⇒ 静默死、
   * 一点线索都没有。实测：内核早期 kprintf 里出问题时正是这种"没有任何输出"。
   * 这里在第一次 kprintf（platform_init 之后 cpu_init 里）之前就把 VBAR 装好。 */
  asm volatile("msr vbar_el1, %0" : : "r"((u64)exception_vectors) : "memory");
  asm volatile("isb");

  /* 打开调用：现在 Pi5 分支只对已确认的 serial10（0x107D001000）幂等重设
   * LCRH/CR，不碰任何 legacy GPIO，也不动波特率 ⇒ 安全（boot 阶段同款操作
   * 已实测可用）。作用是万一固件交接时 TX 被关，内核也能自己把控制台拉起来。 */
  uart_init();
  io_add_write_channel(uart_send);
}

void platform_end(void) {}

void platform_map(void) {
  // RP1 南桥窗口（经 PCIe 暴露到 CPU 侧 0x1F00000000）：只映射用到的前 1MB
  // （UART0 +0x30000、IO_BANK0 +0xD0000、RIO +0xE0000、PADS +0xF0000）。
  // 【必须用 64 位】0x1F00000000 截断成 u32 会变成 0 —— 那样会把低 16MB
  // （内核恒等映射、异常向量、页表所在处）按 Device 重映射。
  for (u64 addr = RP1_WINDOW_BASE; addr < RP1_WINDOW_BASE + RP1_WINDOW_LENGTH;
       addr += 0x1000) {
    page_map((vaddr_t)addr, (vaddr_t)addr, PAGE_DEV);
  }

  /* 【RP1 的另一个窗口（非预取 BAR0）也要映射】DTB 推出来的 RP1 UART0 是
   * 0x1F00030000，而固件日志/ gpio.h 用的是 0x1C00030000 —— 哪个是活的还没
   * 定论（boot 阶段现在是只写广播到三处）。内核 uart_send 也要广播，
   * 所以这里把 0x1F 窗口的前 1MB 一并按设备映射，避免写它时翻译错误。 */
  for (u64 addr = 0x1F00000000ULL; addr < 0x1F00000000ULL + RP1_WINDOW_LENGTH;
       addr += 0x1000) {
    page_map((vaddr_t)addr, (vaddr_t)addr, PAGE_DEV);
  }

  // SoC 侧：调试串口 UART10（0x7D001000，固件日志所在）与 GIC-400（0x7FFF9000）。
  for (u64 addr = SOC_UART10_BASE; addr < SOC_UART10_BASE + SOC_UART10_LENGTH;
       addr += 0x1000) {
    page_map((vaddr_t)addr, (vaddr_t)addr, PAGE_DEV);
  }
  for (u64 addr = GICD_BASE; addr < GICD_BASE + SOC_GIC_LENGTH; addr += 0x1000) {
    page_map((vaddr_t)addr, (vaddr_t)addr, PAGE_DEV);
  }

  // GIC-400 distributor and cpu interface pages (above 32-bit region)
  page_map((vaddr_t)GICD_BASE & ~0xFFFULL, (vaddr_t)GICD_BASE & ~0xFFFULL,
           PAGE_DEV);
  page_map((vaddr_t)GICC_BASE & ~0xFFFULL, (vaddr_t)GICC_BASE & ~0xFFFULL,
           PAGE_DEV);
}

int interrupt_get_source(u32 no) {
  /* 【老的兜底路径】注册 irq_chip 后由 irq_claim_and_dispatch() 接管，这里只在
   * 未注册 chip 时用到；取号/EOI 走通用 GICv2 驱动（gic_irqwho/gic_irqack）。 */
  u32 iar = gic_irqwho();
  u32 id = iar & 0x3FF;

  if (id == GIC_SPURIOUS_ID) {
    return EX_NONE;
  }

  // Acknowledge / end the interrupt before dispatch.
  gic_irqack(id);

  if (id == GIC_PPI_CNTVIRQ) {
    return EX_TIMER;
  }

  // SGIs (0-15) are used for IPI.
  if (id < 16) {
    return EX_IRQ;
  }

  // Unknown SPI: already EOIed, ignore to avoid storms until a real
  // driver handles it.
  return EX_NONE;
}

void ipi_enable(int cpu) {
  if (cpu < 0 || cpu >= MAX_CPU) return;
  // SGI0 is used for IPI (GICD_ISENABLER0 is banked per cpu)
  gic_irq_enable(0);
}

/* PSCI SMC. 返回值按规范是有符号错误码：0 成功，-1 不支持，-2 参数非法，
 * -4 ALREADY_ON。 */
static u64 psci_call(u64 fn, u64 arg1, u64 arg2, u64 arg3) {
  register u64 x0 asm("x0") = fn;
  register u64 x1 asm("x1") = arg1;
  register u64 x2 asm("x2") = arg2;
  register u64 x3 asm("x3") = arg3;
  asm volatile("smc #0"
               : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
               :
               : "memory", "cc", "x4", "x5", "x6", "x7", "x8", "x9", "x10",
                 "x11", "x12", "x13", "x14", "x15", "x16", "x17");
  return x0;
}

static void cache_clean_u64(u64 addr) {
  asm volatile("dc cvac, %0" ::"r"(addr) : "memory");
}

static void publish_u64(u64 addr, u64 val) {
  *(volatile u64 *)addr = val;
  cache_clean_u64(addr);
}

/* 从核看不到主核 D-cache 里的新写入，只能读 DRAM。页表和 boot_info
 * 不够，.data/.bss 也必须落到内存，否则从核起不来。
 * 128MB 覆盖内核镜像和堆。这不是核间侦听，只让从核能把启动数据读出来。 */
static void publish_low_ram(void) {
  static u8 done;
  if (done) {
    return;
  }
  done = 1;
  for (u64 a = 0x80000UL; a < 0x80000UL + 128UL * 1024 * 1024; a += 64) {
    asm volatile("dc civac, %0" ::"r"(a) : "memory");
  }
  asm volatile("dsb sy" ::: "memory");
}

/* 【本轮实验】信箱两套停车点（TF-A 0x100/0x108、老 armstub 0xd8）都不再写。
 * 放行完全交给 PSCI CPU_ON：TF-A 持有 ENTRYPOINT（bl31_warm_entrypoint），
 * 我们 SMC 传的 entry（apu_entry）由它恢复上下文后 ERET 到 EL1。 */
/* 【实验】不再直接写 BL31 的可信信箱（0x100=ENTRYPOINT、0x108+n*8=hold）。
 * 此前我们先写 GO 再发 SMC：停车核被我们的 GO 唤醒后从 EL3 直接 br 进
 * apu_entry，完全绕过 TF-A 的 CPU_ON 热启动路径（psci 上下文恢复 + 按我们
 * SMC 传入的 entry ERET 回 EL1）。已知能跑的系统（Linux/Urthr）都只发 SMC、
 * 从不碰信箱；我们是目前唯一的旁路者。本轮纯 PSCI：rpi5_pwr_domain_on 自己
 * 写 GO，停车核经 bl31_warm_entrypoint 恢复上下文后 ERET 到 apu_entry。
 * 判读：分裂消失 ⇒ 根因是信箱旁路；AP 根本不出现 ⇒ 看 [mp] rc 的返回码。 */
static void park_release(u32 cpu, u64 entry) {
  (void)cpu;
  (void)entry;
  asm volatile("dsb sy" ::: "memory");
  asm volatile("sev" ::: "memory");
}

// Pi5：DT enable-method="psci"。CPU_ON 的目标是 MPIDR affinity，
// BCM2712 上是 Aff1：cpu1=0x100、cpu2=0x200、cpu3=0x300。传 1/2/3 会
// INVALID_PARAMETERS，从核一直停在 BL31。入口用 apu_entry（链接地址即物理地址）。
void lcpu_send_start(u32 cpu, u64 entry) {
  (void)entry;
  if (cpu <= 0 || cpu >= MAX_CPU) return;

  u64 ep = (u64)&apu_entry;
  u64 aff = (u64)cpu << 8;

  /* 【实验】从核进门先非缓存读 ap_ttbr（一条 64B 行装着 ttbr0/mair/tcr，
   * 见 boot-armv8-a.s ap_el1_ready），然后在汇编里直接开 MMU+cache 才进
   * C——避免从核以 M=C=0 跑启动 C 代码时把主核已缓存的行拆出侦听域。
   * 这里放行前用主核当前寄存器值写好并清缓存，之后不再重写。 */
  {
    static u8 pub;
    if (!pub) {
      u64 v;
      pub = 1;
      asm volatile("mrs %0, ttbr0_el1" : "=r"(v));
      ap_ttbr[0] = v;
      asm volatile("mrs %0, mair_el1" : "=r"(v));
      ap_ttbr[1] = v;
      asm volatile("mrs %0, tcr_el1" : "=r"(v));
      ap_ttbr[2] = v;
      cache_clean_u64((u64)ap_ttbr);
      asm volatile("dsb sy" ::: "memory");
    }
  }

  ap_release[cpu] = 1;
  cache_clean_u64((u64)&ap_release[cpu]);
  asm volatile("dsb sy" ::: "memory");
  asm volatile("sev" ::: "memory");

  publish_low_ram();
  park_release(cpu, ep);

  u64 rc = psci_call(0xC4000003UL, aff, ep, 0);
  if (rc == (u64)-1) {
    rc = psci_call(0x84000003UL, aff, ep, 0);
  }
  if (rc == (u64)-2) {
    rc = psci_call(0xC4000003UL, cpu, ep, 0);
  }
  asm volatile("dsb sy" ::: "memory");
  asm volatile("sev" ::: "memory");
}

void lcpu_wait_start(int cpu) {
  if (cpu <= 0 || cpu >= MAX_CPU) return;
  while (!ap_release[cpu]) {
    asm volatile("wfe" ::: "memory");
  }
}

void ipi_send(int cpu, int vec) {
  if (cpu < 0 || cpu >= MAX_CPU) return;
  // GICD_SGIR: filter=0 (target list), cpu mask, sgi id
  gic_send_sgi(cpu, vec);
  dsb();
}

void ipi_clear(int cpu) {
  if (cpu < 0 || cpu >= MAX_CPU) return;
  // Read + EOI pending SGI to clear it.
  u32 iar = gic_irqwho();
  if ((iar & 0x3FF) != GIC_SPURIOUS_ID) {
    gic_irqack(iar & 0x3FF);
  }
}
