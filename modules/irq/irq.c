/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
/* irq：把统一中断框架的 IRQ 表导出到用户态（/dev/irq）。
 *
 * 设计文档：docs/develop/architecture/中断子系统设计.md
 *   §3.3 —— "诊断导出"是"模块化"的正确落点（可选、不在关键路径）
 *   §12.4 —— 诊断出口的候选之一
 *
 * 用法（用户态）：cat /dev/irq
 *
 * 【为什么需要它】统一中断框架只提供内核侧的 irq_dump()（走 kprintf），
 * 用户态没有任何办法看到"每个 IRQ 号的名字/计数/mask 状态"。排障时只能靠
 * 串口日志猜 —— 而日志本身也可能因为打印路径出问题而不可信（例如多核共用
 * 打印缓冲导致的日志停摆）。有了 /dev/irq，一条 cat 就能拿到整张表。
 *
 * 只读、不改变任何中断行为；每次 read 重新生成，所以看到的是实时计数。 */
#include "dev/devfs.h"
#include "kernel/kernel.h"
#include "kernel/irq.h"
#include "kernel/string.h"

/* dev/devfs.c 里的字符设备 vnode 操作集：除 read 外全部复用
 * （read 要换成带 offset 的实现，见 irq_read 的说明） */
extern voperator_t device_operator;

/* 【缓冲大小 1024 不是随手取的】2048 那版在 raspi3 上会稳定起不来（0/5），
 * 1024 则 3/3 正常 —— 说明镜像里存在"越界写、受害者随 .bss 布局变化"的隐患
 * （同类问题已抓到一次：printf_buffer 越界踩到紧邻的 print_lock）。
 * 在这个隐患查清之前，这里刻意用已被验证过的 1024。
 * 表格行很短（一行 ~30 字节），1024 够列十来行；空间不足时下面的循环会优雅截断。 */
static char irq_buf[1024];
static voperator_t irq_operator;

/* 把整张表格式化进静态缓冲，返回长度。
 * 用带界的 ksnprintf：绝不能越界（这块缓冲紧邻其它 .bss） */
static int irq_format(void) {
  int off = 0;
  int n;
  u32 irq;

  /* 表头与下面的行格式逐列对齐：%3u %6u %10u %9u %5u %3u  %s */
  n = ksnprintf(irq_buf, sizeof(irq_buf),
                "IRQ  COUNT UNHANDLED SPURIOUS  MASK  EN  NAME\n");
  if (n > 0) {
    off += n;
  }

  /* 平台还没注册 irq_chip ⇒ 仍走老的 interrupt_get_source()，表里自然没东西 */
  if (!irq_core_enabled()) {
    ksnprintf(irq_buf + off, sizeof(irq_buf) - off,
              "(no irq_chip registered: this platform still uses the legacy"
              " interrupt_get_source() path)\n");
    return (int)kstrlen(irq_buf);
  }

  for (irq = 0; irq < IRQ_NUM_MAX; irq++) {
    struct irq_desc* d = irq_get_desc(irq);
    const char* name;

    if (d == NULL) {
      break;
    }
    /* 只列"有内容"的号：绝大多数号是空的，全列出来反而看不清 */
    if (d->count == 0 && d->unhandled == 0 && d->spurious == 0 &&
        d->action == NULL) {
      continue;
    }
    /* 一行最长约 80 字节：空间不够就停，避免只剩半行 */
    if (off > (int)sizeof(irq_buf) - 96) {
      break;
    }
    name = irq_get_name(irq);
    n = ksnprintf(irq_buf + off, sizeof(irq_buf) - off,
                  "%3u %6u %10u %9u %5u %3u  %s\n", irq, d->count,
                  d->unhandled, d->spurious, d->masked, d->enabled,
                  name ? name : "-");
    if (n <= 0) {
      break;
    }
    off += n;
  }
  return (int)kstrlen(irq_buf);
}

/* 【为什么要自己实现 vnode 级 read】设备层的 dev_read_fn 没有 offset 参数，
 * 设备无法表达"读完了"，而 cat 是循环 read 到 EOF 的 ⇒ 会无限重复输出整张表。
 * vnode 操作集里的 read 带 offset（由 VFS 维护）：offset 到位就返回 0，
 * cat 自然结束，并且天然支持分段读。注意 vread_t 的第三个形参是 u32。 */
static u32 irq_read(vnode_t* node, u32 offset, u32 nbytes, u8* buffer) {
  int total;
  u32 left;

  (void)node;
  if (buffer == NULL || nbytes == 0) {
    return 0;
  }
  total = irq_format();
  if (offset >= (u32)total) {
    return 0; /* EOF */
  }
  left = (u32)total - offset;
  if (left > nbytes) {
    left = nbytes;
  }
  kmemcpy(buffer, irq_buf + offset, left);
  return left;
}

/* module_t.init 的类型是 void (*)() ⇒ 这里用 void 返回 */
void irq_init(void) {
  device_t* dev;
  vnode_t* node;

  dev = kmalloc(sizeof(device_t), DEFAULT_TYPE);
  if (dev == NULL) {
    kprintf("irq: no memory\n");
    return;
  }
  dev->name = "irq";
  dev->id = DEVICE_IRQ;
  dev->type = DEVICE_TYPE_CHAR;
  dev->read = NULL; /* 走下面 vnode 级的 read（需要 offset） */
  device_add(dev);

  node = vfs_create_node("irq", V_FILE);
  if (node == NULL) {
    kprintf("irq: create /dev/irq failed\n");
    return;
  }
  vfs_mount(NULL, "/dev", node);
  node->device = dev;

  /* 复用 devfs 的操作集，只把 read 换成带 offset 的实现 */
  irq_operator = device_operator;
  irq_operator.read = irq_read;
  node->op = &irq_operator;

  log_info("irq: /dev/irq ready\n");
}

void irq_exit(void) { log_info("irq exit\n"); }

module_t irq_module = {
    .name = "irq", .init = irq_init, .exit = irq_exit};
