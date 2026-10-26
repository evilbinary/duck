/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 *
 * 最小 evdev 实现：/dev/input/event0（相对鼠标）。
 *
 * 为什么需要它：Xorg 的输入驱动（xf86-input-evdev）直接 open
 * /dev/input/event0，用 ioctl 读能力位图、用 read 收 struct input_event。
 * 本文件只提供"一块相对指针设备"所需的子集：EV_REL(X/Y/WHEEL) +
 * EV_KEY(BTN_LEFT/RIGHT/MIDDLE) + EV_SYN(SYN_REPORT)。
 *
 * 数据源是平台鼠标驱动（device_find(DEVICE_MOUSE) 的 read，返回 PS/2
 * 三字节包），因此 /dev/mouse 与 /dev/input/event0 共用同一份硬件输入。
 ********************************************************************/
#include "kernel/kernel.h"
#include "kernel/string.h" /* kstrncpy/kmemset */
#include "dev/devfs.h"     /* device_operator：/dev/input/event0 的节点算子 */

#define EVDEV_PHY_NAME "YiYiYa Virtual Mouse"

/* Linux input 常量（uapi/linux/input-event-codes.h 的最小子集） */
#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_REL 0x02

#define SYN_REPORT 0x00
#define SYN_DROPPED 0x03

#define REL_X 0x00
#define REL_Y 0x01
#define REL_WHEEL 0x08

#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111
#define BTN_MIDDLE 0x112

/* aarch64 glibc: struct timeval = {i64, i64}，input_event 共 24 字节 */
typedef struct input_event {
  i64 sec;
  i64 usec;
  u16 type;
  u16 code;
  i32 value;
} input_event_t;

#define EVDEV_QUEUE_SIZE 64

static input_event_t evdev_queue[EVDEV_QUEUE_SIZE];
static u32 evdev_head;
static u32 evdev_tail;
static u32 evdev_btn_old;

static void evdev_push(u16 type, u16 code, i32 value) {
  u32 next = (evdev_head + 1) % EVDEV_QUEUE_SIZE;
  if (next == evdev_tail) {
    return; /* 队列满：丢最新，避免覆盖未读事件 */
  }
  evdev_queue[evdev_head].type = type;
  evdev_queue[evdev_head].code = code;
  evdev_queue[evdev_head].value = value;
  evdev_head = next;
}

/* 从平台鼠标驱动取一份 PS/2 包并展开成 input_event 序列。
 * 【没有变化就不投递】QEMU 的 HID 空闲时会周期性上报全零报告，
 * 若照单全收，Xorg 会空转（每 10ms 一次 read）。 */
static void evdev_pull(void) {
  device_t* mouse = device_find(DEVICE_MOUSE);
  if (mouse == NULL || mouse->read == NULL) {
    return;
  }

  u8 pkt[4];
  size_t n = mouse->read(mouse, pkt, sizeof(pkt));
  if (n < 3) {
    return; /* 硬件侧暂无新报告 */
  }

  i32 dx = (i32)(i8)pkt[1];
  i32 dy = (i32)(i8)pkt[2];
  u32 btn = pkt[0] & 0x07;

  if (dx == 0 && dy == 0 && btn == evdev_btn_old) {
    return;
  }

  if (dx != 0) {
    evdev_push(EV_REL, REL_X, dx);
  }
  if (dy != 0) {
    /* 【不要取反】usb_mouse_read_packet 交出的 byte2 就是 USB HID 报告的
     * dy，正向已经是屏幕方向（向下为正），与 EV_REL/REL_Y 的约定一致。
     * 之前按"PS/2 的 Y 向上为正"在这里写成 -dy，实测光标上下相反。 */
    evdev_push(EV_REL, REL_Y, dy);
  }
  if ((btn & 0x01) != (evdev_btn_old & 0x01)) {
    evdev_push(EV_KEY, BTN_LEFT, (btn & 0x01) ? 1 : 0);
  }
  if ((btn & 0x02) != (evdev_btn_old & 0x02)) {
    evdev_push(EV_KEY, BTN_RIGHT, (btn & 0x02) ? 1 : 0);
  }
  if ((btn & 0x04) != (evdev_btn_old & 0x04)) {
    evdev_push(EV_KEY, BTN_MIDDLE, (btn & 0x04) ? 1 : 0);
  }
  evdev_btn_old = btn;

  evdev_push(EV_SYN, SYN_REPORT, 0);
}

/* 【重入保护】USB 驱动不可重入，而这个抽包动作会从三个上下文发起：
 * read（设备线程）、select/poll/epoll 的就绪判定（等待者线程）、
 * poll_wait_notify（唤醒方线程）。同一时刻只允许一个抽包者。 */
static volatile int evdev_pumping;

static void evdev_pump(void) {
  if (evdev_head != evdev_tail) {
    return; /* 队列里还有未读事件，先不碰硬件 */
  }
  if (evdev_pumping) {
    return;
  }
  evdev_pumping = 1;
  evdev_pull();
  evdev_pumping = 0;
}

/* 供 select/poll/epoll 判定"这个 fd 现在可不可读"。必须给真实答案：
 * 内核 sys_fd_ready_t 对非 pipe/socket 的 fd 原本一律"存在即就绪"，
 * 于是 epoll_pwait 立刻返回、Xorg 的 read 拿到 EAGAIN 后马上再进 epoll_pwait
 * ——InputThread 就是这样空转烧掉 30%+ CPU 的。
 * 由 posix 层按 device->id == DEVICE_EVDEV 调用（弱符号，见 sysfn_fs.c）。 */
int evdev_poll_ready(void* arg) {
  (void)arg;
  evdev_pump();
  return evdev_head != evdev_tail;
}

static size_t evdev_read(device_t* dev, void* buf, size_t len) {
  (void)dev;
  if (buf == NULL || len < sizeof(input_event_t)) {
    return 0;
  }
  evdev_pump();
  if (evdev_head == evdev_tail) {
    /* 【不能返回 0】Xorg 的 evdev 驱动把 read()==0 当 EOF（设备断开），
     * 读到一次就把设备从 select 集合里摘掉，之后鼠标再动也没人 read。
     * 【不要在这里挂起】试过 poll_wait_suspend(5ms)：它和 X 自己的
     * select 等待冲突，内核打 "PWFILL tid=N reason=1 ret=0" 后 Xorg
     * 卡死在 LoadModule 之后。这里的 read 只做"拉一次硬件"这一件事，
     * 降频由就绪判定 + 短周期重查负责（sysfn.h 的 SYS_POLLED_FD_RECHECK_MS）。 */
    return (size_t)-11; /* -EAGAIN */
  }
  input_event_t* out = (input_event_t*)buf;
  *out = evdev_queue[evdev_tail];
  evdev_tail = (evdev_tail + 1) % EVDEV_QUEUE_SIZE;
  return sizeof(input_event_t);
}

static void evdev_set_bit(u8* buf, u32 bytes, u32 bit) {
  if ((bit >> 3) < bytes) {
    buf[bit >> 3] |= (u8)(1u << (bit & 7));
  }
}

/* Xorg 的 evdev 驱动靠这组 ioctl 判断"这块设备能不能当鼠标"：
 *   EVIOCGVERSION/EVIOCGID/EVIOCGNAME → 基本身份
 *   EVIOCGBIT(EV_SYN/EV_KEY/EV_REL)   → 能力位图（EV_REL+REL_X/REL_Y 是相对指针的关键）
 *   EVIOCGABS                         → 绝对设备才用，这里返回全 0
 * 命令编码按 Linux _IOC：nr=bit0-7, type=bit8-15, size=bit16-29。 */
/* 签名必须与 dev_ioctl_fn（size_t (*)(device_t*, u32, ...)）一致：
 * device_ioctl 以位置参数传用户缓冲区，这里用 va_arg 取回。 */
static size_t evdev_ioctl(device_t* dev, u32 cmd, ...) {
  (void)dev;
  u32 nr = cmd & 0xff;
  u32 type = (cmd >> 8) & 0xff;
  u32 size = (cmd >> 16) & 0x3fff;
  u8* out;
  va_list ap;

  va_start(ap, cmd);
  out = (u8*)va_arg(ap, void*);
  va_end(ap);

  if (type != 0x45) { /* 'E' */
    return 0;
  }

  switch (nr) {
    case 0x01: /* EVIOCGVERSION */
      if (out != NULL) {
        *(i32*)out = 0x010001;
      }
      return 0;
    case 0x02: { /* EVIOCGID */
      struct {
        u16 bus;
        u16 vendor;
        u16 product;
        u16 version;
      } id;
      id.bus = 0x03; /* BUS_USB */
      id.vendor = 0x0627;
      id.product = 0x0001;
      id.version = 0x0111;
      if (out != NULL) {
        kmemcpy(out, &id, sizeof(id));
      }
      return 0;
    }
    case 0x06: /* EVIOCGNAME */
      if (out != NULL && size > 0) {
        kstrncpy((char*)out, EVDEV_PHY_NAME, size - 1);
        ((char*)out)[size - 1] = 0;
      }
      return 0;
    case 0x07: /* EVIOCGPHYS */
      if (out != NULL && size > 0) {
        kstrncpy((char*)out, "yiyiya/input0", size - 1);
        ((char*)out)[size - 1] = 0;
      }
      return 0;
    case 0x08: /* EVIOCGUNIQ */
      if (out != NULL && size > 0) {
        ((char*)out)[0] = 0;
      }
      return 0;
    case 0x09: /* EVIOCGPROP */
    case 0x18: /* EVIOCGKEY */
    case 0x19: /* EVIOCGLED */
    case 0x1a: /* EVIOCGSND */
    case 0x1b: /* EVIOCGSW */
      if (out != NULL && size > 0) {
        kmemset(out, 0, size);
      }
      return 0;
    case 0x90: /* EVIOCGRAB：独占成功 */
    case 0x91: /* EVIOCREVOKE */
    case 0xa0: /* EVIOCSCLOCKID */
      return 0;
    default:
      break;
  }

  if (nr >= 0x20 && nr < 0x40) { /* EVIOCGBIT(ev, len) */
    u32 ev = nr - 0x20;
    if (out == NULL || size == 0) {
      return 0;
    }
    kmemset(out, 0, size);
    if (ev == EV_SYN) {
      /* EVIOCGBIT(0) 返回的是"支持哪些事件类型"的位图：bit N 对应 EV_N。
       * 这里必须点亮 EV_SYN/EV_KEY/EV_REL 本身，而不是 SYN_REPORT/SYN_DROPPED
       * 这两个 SYN 代码位——写错会让驱动认为设备既无按键也无相对轴，
       * 直接报 "Don't know how to use device" 把设备丢掉。 */
      evdev_set_bit(out, size, EV_SYN);
      evdev_set_bit(out, size, EV_KEY);
      evdev_set_bit(out, size, EV_REL);
    } else if (ev == EV_KEY) {
      evdev_set_bit(out, size, BTN_LEFT);
      evdev_set_bit(out, size, BTN_RIGHT);
      evdev_set_bit(out, size, BTN_MIDDLE);
    } else if (ev == EV_REL) {
      evdev_set_bit(out, size, REL_X);
      evdev_set_bit(out, size, REL_Y);
      evdev_set_bit(out, size, REL_WHEEL);
    }
    return 0;
  }

  if (nr >= 0x40 && nr < 0x60) { /* EVIOCGABS(abs)：非绝对设备 */
    if (out != NULL && size > 0) {
      kmemset(out, 0, size);
    }
    return 0;
  }

  return 0;
}

/* 模块 init 是 void (*)()（module.h 的 mod_init_fn）：失败只能打日志返回，
 * 没有错误码通道。 */
static void evdev_init(void) {
  vnode_t* dir = vfs_create_node("input", V_DIRECTORY);
  if (dir == NULL) {
    log_error("evdev: create /dev/input failed\n");
    return;
  }
  vfs_mount(NULL, "/dev", dir);

  device_t* dev = kmalloc(sizeof(device_t), KERNEL_TYPE);
  if (dev == NULL) {
    log_error("evdev: alloc device failed\n");
    return;
  }
  kmemset(dev, 0, sizeof(device_t));
  dev->name = "event0";
  dev->id = DEVICE_EVDEV;
  dev->type = DEVICE_TYPE_CHAR;
  dev->read = evdev_read;
  dev->ioctl = evdev_ioctl;
  device_add(dev);

  vnode_t* node = vfs_create_node("event0", V_FILE);
  if (node == NULL) {
    log_error("evdev: create event0 node failed\n");
    return;
  }
  node->device = dev;
  node->op = &device_operator;
  vfs_add_child(dir, node);

  evdev_head = 0;
  evdev_tail = 0;
  evdev_btn_old = 0;
  log_info("evdev: /dev/input/event0 ready\n");
}

void evdev_exit(void) { log_info("evdev exit\n"); }

module_t input_module = {
    .name = "input", .init = evdev_init, .exit = evdev_exit};
