/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "io.h"
#include "common.h"
#include "string.h"

#include "stdarg.h"

write_channel_fn write_channels[10];
u32 write_channel_number = 0;

void io_add_write_channel(write_channel_fn fn) {
  for (int i = 0; i < write_channel_number; i++) {
    if (fn == write_channels[i]) {
      return;
    }
  }
  write_channels[write_channel_number++] = fn;
}

void print_char(u8 ch) {
  for (int i = 0; i < write_channel_number; i++) {
    write_channel_fn fn = write_channels[i];
    if (fn != NULL) {
      fn(ch);
    }
  }
}

char printf_buffer[KPRINT_BUF];

/* 所有 CPU 共用 printf_buffer 和同一个控制台：并发打印会互相覆盖字符串，
 * 实测日志被打成乱码（探针数据完全取不出来）。这里加锁——有界自旋：
 * 持有者卡死（例如同 CPU 在异常里重入打印）时放弃抢锁，
 * 宁可输出交错也绝不死锁。
 *
 * 【本文件保持平台无关】通用层不认识任何架构寄存器/符号，只认架构层设置的
 * 一个开关 io_print_lock_set_atomic()：
 *   1 → 用 __sync_* 内建（多核真正原子，历史行为，也是缺省值）；
 *   0 → 用普通 load/store 自旋（完全不用独占指令，任何内存属性下都安全）。
 * 为什么需要这个开关：ldrex/strex 只对 Normal 内存有定义，ARMv7 在 MMU 关闭时
 * 全部内存都是 Strongly-ordered，独占访问是 UNPREDICTABLE —— Cortex-A7 实机
 * 直接报同步外部异常，而且位置正好落在 kprintf 里（V3s 实测：串口停在 boot
 * 打印的 "kernel entry 42002250" 之后再无输出）：
 *   data abort  pc=<strex 的下一条>  DFSR=0x1008(WnR=1)  DFAR=<&print_lock>
 * 而内核第一次 kprintf 远早于 page_init()/mm_page_enable()：V3s 是
 * platform_init→cpu_clock_init→cpu_get_rate 里那句 "V3S_CCU_BASE ..."，
 * 其后 mm_init/mm_parse_map/mm_page_enable 还有一串打印，全部跑在"MMU 关着"
 * 的状态下（树莓派是固件先开好 MMU 才进内核，所以只有 sunxi 这类 U-Boot 引导、
 * 进内核时 MMU 仍然是关的平台会被打死）。
 * 由架构层负责在第一次 kprintf 之前初始化本开关（armv7-a 在 arch_init() 里按
 * SCTLR.M 设置），并在 MMU 真正开关的那一刻跟着翻转
 * （armv7-a 的 cpu_enable_page()/cpu_disable_page()）。 */
static volatile int print_lock_atomic = 1;

void io_print_lock_set_atomic(int on) { print_lock_atomic = on; }

static volatile int print_lock;

int kprintf(const char* fmt, ...) {
  int locked = 0;
  int spins = 0;
  int atomic = print_lock_atomic;

  if (atomic) {
    while (__sync_lock_test_and_set(&print_lock, 1)) {
      if (++spins > 200000) break;
    }
  } else {
    while (print_lock) {
      if (++spins > 200000) break;
    }
    if (spins <= 200000) {
      print_lock = 1;
    }
  }
  locked = (spins <= 200000);

  kmemset(printf_buffer, 0, KPRINT_BUF);
  int i = 0;
  va_list args;
  va_start(args, fmt);
  i = kvsprintf(printf_buffer, fmt, args);
  va_end(args);

  int len = kstrlen(printf_buffer);
  if (i > KPRINT_BUF) {
    len = KPRINT_BUF-1;
    // OVER PRINT
    print_char('O');
    print_char('V');
    print_char('E');
    print_char('R');
    print_char(' ');
    print_char('P');
    print_char('R');
    print_char('I');
    print_char('N');
    print_char('T');
    print_char('\n');
  }

  for (int i = 0; i < len; i++) {
    print_char(printf_buffer[i]);
  }
  if (locked) {
    if (atomic) {
      __sync_lock_release(&print_lock);
    } else {
      print_lock = 0;
    }
  }
  return i;
}