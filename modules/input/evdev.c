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
#include "dev/devfs.h"

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
    /* PS/2 的 Y 向上为正，evdev/X11 的屏幕坐标向下为正 */
    evdev_push(EV_REL, REL_Y, -dy);
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

static size_t evdev_read(device_t* dev, void* buf, size_t len) {
  (void)dev;
  if (buf == NULL || len < sizeof(input_event_t)) {
    return 0;
  }
  if (evdev_head == evdev_tail) {
    evdev_pull();
    if (evdev_head == evdev_tail) {
      return 0; /* 没有事件：调用方（libevdev/Xorg）按非阻塞轮询处理 */
    }
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
static size_t evdev_ioctl(device_t* dev, u32 cmd, void* args) {
  (void)dev;
  u32 nr = cmd & 0xff;
  u32 type = (cmd >> 8) & 0xff;
  u32 size = (cmd >> 16) & 0x3fff;
  u8* out = (u8*)args;

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

static int evdev_init(void) {
  vnode_t* dir = vfs_create_node("input", V_DIRECTORY);
  if (dir == NULL) {
    log_error("evdev: create /dev/input failed\n");
    return -1;
  }
  vfs_mount(NULL, "/dev", dir);

  device_t* dev = kmalloc(sizeof(device_t), KERNEL_TYPE);
  if (dev == NULL) {
    log_error("evdev: alloc device failed\n");
    return -1;
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
    return -1;
  }
  node->device = dev;
  node->op = &device_operator;
  vfs_add_child(dir, node);

  evdev_head = 0;
  evdev_tail = 0;
  evdev_btn_old = 0;
  log_info("evdev: /dev/input/event0 ready\n");
  return 0;
}

void evdev_exit(void) { log_info("evdev exit\n"); }

module_t input_module = {
    .name = "input", .init = evdev_init, .exit = evdev_exit};
