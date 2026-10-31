/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 *
 * 内核 input 设备核心（见 input_core.h 的说明）：
 *   · 能力位图 / absinfo / 属性位 / 按键状态 的登记；
 *   · /dev/input/eventN 节点创建（device_t + device_operator）；
 *   · 带时间戳的事件队列与 read；
 *   · Linux evdev ioctl 全覆盖（EVIOCG* 系列）。
 ********************************************************************/
#include "input_core.h"

#include "dev/devfs.h"
#include "kernel/memory.h"
#include "kernel/schedule.h"
#include "kernel/stat.h" /* IOC_STAT */
#include "kernel/stdarg.h"
#include "kernel/string.h"
#include "kernel/vfs.h"

static input_dev_t* input_devs[INPUT_MAX_DEVICES];
static vnode_t* input_dir;
static int input_next_index;

/* ---------------- 位图辅助 ---------------- */
static void in_bit_set(u32* bits, u32 code) {
  bits[code / 32] |= (u32)1u << (code % 32);
}

static int in_bit_get(const u32* bits, u32 code) {
  return (int)((bits[code / 32] >> (code % 32)) & 1u);
}

static u32 in_bit_nbytes(const u32* bits, u32 nwords, u32 max_bytes) {
  u32 bytes = nwords * 4;
  if (bytes > max_bytes) {
    bytes = max_bytes;
  }
  return bytes;
}

/* ---------------- 分配与能力登记 ---------------- */
input_dev_t* input_alloc_device(const char* name) {
  input_dev_t* dev = kmalloc(sizeof(input_dev_t), KERNEL_TYPE);
  if (dev == NULL) {
    return NULL;
  }
  kmemset(dev, 0, sizeof(input_dev_t));
  if (name != NULL) {
    kstrncpy(dev->name, name, INPUT_NAME_MAX - 1);
  }
  dev->index = -1;
  dev->base.name = dev->name;
  dev->base.id = DEVICE_EVDEV;
  dev->base.type = DEVICE_TYPE_CHAR;
  return dev;
}

void input_set_ids(input_dev_t* dev, u16 bustype, u16 vendor, u16 product,
                   u16 version) {
  if (dev == NULL) {
    return;
  }
  dev->bustype = bustype;
  dev->vendor = vendor;
  dev->product = product;
  dev->version = version;
}

void input_set_type(input_dev_t* dev, u32 type) {
  if (dev == NULL || type > EV_MAX) {
    return;
  }
  in_bit_set(dev->ev_bits, type);
}

void input_set_code(input_dev_t* dev, u32 type, u32 code) {
  if (dev == NULL) {
    return;
  }
  switch (type) {
    case EV_KEY:
      if (code <= KEY_MAX) {
        in_bit_set(dev->key_bits, code);
      }
      break;
    case EV_REL:
      if (code <= REL_MAX) {
        in_bit_set(dev->rel_bits, code);
      }
      break;
    case EV_ABS:
      if (code <= ABS_MAX) {
        in_bit_set(dev->abs_bits, code);
      }
      break;
    case EV_SW:
      if (code <= SW_MAX) {
        in_bit_set(dev->sw_bits, code);
      }
      break;
    default:
      break;
  }
}

void input_set_absinfo(input_dev_t* dev, u32 code, i32 minimum, i32 maximum,
                       i32 fuzz, i32 flat, i32 resolution) {
  if (dev == NULL || code > ABS_MAX) {
    return;
  }
  dev->absinfo[code].minimum = minimum;
  dev->absinfo[code].maximum = maximum;
  dev->absinfo[code].fuzz = fuzz;
  dev->absinfo[code].flat = flat;
  dev->absinfo[code].resolution = resolution;
}

void input_set_prop(input_dev_t* dev, u32 prop) {
  if (dev == NULL || prop > INPUT_PROP_MAX) {
    return;
  }
  in_bit_set(dev->prop_bits, prop);
}

/* ---------------- 事件队列 ---------------- */
/* 时间戳用单调时钟（与 clock_gettime(CLOCK_MONOTONIC) 同源）：libinput 会把
 * evdev 的 clock 设成 CLOCK_MONOTONIC，用实时钟会让事件时间戳跳到"未来"
 * 或"过去"，超时/手势判定全乱。 */
static void in_stamp(input_event_t* ev) {
  u64 ticks = (u64)schedule_get_ticks();
  ev->sec = (i64)(ticks / SCHEDULE_FREQUENCY);
  ev->usec = (i64)((ticks % SCHEDULE_FREQUENCY) *
                   (1000000u / SCHEDULE_FREQUENCY));
}

static void in_pump(input_dev_t* dev) {
  if (dev->pump == NULL) {
    return;
  }
  if (dev->pumping) {
    return; /* 硬件驱动不可重入：read 与就绪判定可能同时进来 */
  }
  dev->pumping = 1;
  dev->pump(dev);
  dev->pumping = 0;
}

void input_report(input_dev_t* dev, u32 type, u32 code, i32 value) {
  input_event_t* ev;
  u32 next;

  if (dev == NULL) {
    return;
  }
  /* 当前状态：EVIOCGKEY / EVIOCGSW 要按 Linux 语义返回"此刻按住的键" */
  if (type == EV_KEY && code <= KEY_MAX) {
    if (value) {
      in_bit_set(dev->key_state, code);
    } else {
      dev->key_state[code / 32] &= ~((u32)1u << (code % 32));
    }
  } else if (type == EV_SW && code <= SW_MAX) {
    if (value) {
      in_bit_set(dev->sw_state, code);
    } else {
      dev->sw_state[code / 32] &= ~((u32)1u << (code % 32));
    }
  } else if (type == EV_ABS && code <= ABS_MAX) {
    /* EVIOCGABS 的 value 字段与 EVIOCGMTSLOTS 都读当前值 */
    dev->absinfo[code].value = value;
  }

  next = (dev->head + 1) % INPUT_QUEUE_SIZE;
  if (next == dev->tail) {
    /* 队列满：丢掉新事件（与 Linux 的"丢事件 + 下次 SYN_DROPPED"取舍一致的
     * 简化版）。队列 128 项，正常一帧几个事件，只有消费者长期不读才会满。 */
    return;
  }
  ev = &dev->queue[dev->head];
  in_stamp(ev);
  ev->type = (u16)type;
  ev->code = (u16)code;
  ev->value = value;
  dev->head = next;
}

void input_sync(input_dev_t* dev) {
  input_report(dev, EV_SYN, SYN_REPORT, 0);
}

int input_has_event(input_dev_t* dev) {
  if (dev == NULL) {
    return 0;
  }
  in_pump(dev);
  return dev->head != dev->tail;
}

int input_poll_ready(void* arg) {
  return input_has_event((input_dev_t*)arg);
}

/* ---------------- read ---------------- */
static size_t input_core_read(device_t* base, void* buf, size_t len) {
  input_dev_t* dev = (input_dev_t*)base;
  size_t max;
  size_t n = 0;
  u8* out = (u8*)buf;

  if (buf == NULL || len < sizeof(input_event_t)) {
    return 0;
  }
  in_pump(dev);
  if (dev->head == dev->tail) {
    /* 【不能返回 0】用户态把 read()==0 当 EOF（设备断开）；这里回 -EAGAIN，
     * 降频由就绪判定 + SYS_POLLED_FD_RECHECK_MS 负责。 */
    return (size_t)-11;
  }

  /* 【与 Linux 一致：一次 read 取走队列里所有事件（不超过用户缓冲）】
   * 一次鼠标报告是 REL_X + REL_Y + SYN_REPORT 三个事件，旧实现每次只回一个，
   * 用户态就得为同一份报告做 3 次 read（配合 poll 的 8ms 切片，延迟被放大
   * 数倍，光标显得发滞）。 */
  max = len / sizeof(input_event_t);
  while (n < max && dev->head != dev->tail) {
    *(input_event_t*)(out + n * sizeof(input_event_t)) = dev->queue[dev->tail];
    dev->tail = (dev->tail + 1) % INPUT_QUEUE_SIZE;
    n++;
  }
  return n * sizeof(input_event_t);
}

/* ---------------- ioctl（Linux evdev 语义） ----------------
 * cmd 编码按 _IOC：nr = bit0-7，type = bit8-15，size = bit16-29。
 * 参数由 device_ioctl 以 void* 传入（见 kernel/devfn.c），这里用 va_arg 取回。 */
static void in_copy_bits(u8* out, u32 size, const u32* bits, u32 nwords,
                         u32 max_bytes) {
  u32 n;

  if (out == NULL || size == 0) {
    return;
  }
  kmemset(out, 0, size);
  n = in_bit_nbytes(bits, nwords, max_bytes);
  if (n > size) {
    n = size;
  }
  kmemcpy(out, bits, n);
}

/* EVIOCGMTSLOTS(len)：首字节是 ABS_MT_* 码，其后每 slot 一个 i32 值。
 * 单点设备（没有 ABS_MT_SLOT）也返回 0 值，调用方按"slot 0"处理。 */
static void in_mt_slots(input_dev_t* dev, u8* out, u32 size) {
  u32 code;
  u32 nslots;
  u32 i;

  if (out == NULL || size < 4) {
    return;
  }
  code = out[0];
  nslots = (size - 1) / 4;
  for (i = 0; i < nslots; i++) {
    i32 v = 0;
    if (code <= ABS_MAX) {
      v = dev->absinfo[code].value;
    }
    kmemcpy(out + 1 + i * 4, &v, 4);
  }
}

static size_t input_core_ioctl(device_t* base, u32 cmd, ...) {
  input_dev_t* dev = (input_dev_t*)base;
  u32 nr = cmd & 0xff;
  u32 type = (cmd >> 8) & 0xff;
  u32 size = (cmd >> 16) & 0x3fff;
  u8* out;
  va_list ap;

  va_start(ap, cmd);
  out = (u8*)va_arg(ap, void*);
  va_end(ap);

  /* stat：必须是字符设备 + 正确的 rdev。
   * 用户态（libinput 的 path 后端 / udev 的 devnum 反查）靠
   * S_ISCHR + st_rdev → /sys/dev/char/<major>:<minor> 找到设备目录，
   * rdev 编错就整条链断在这里。Linux input：主 13、eventN 次 64+N。 */
  if (cmd == IOC_STAT) {
    struct stat* st = (struct stat*)out;
    if (st != NULL) {
      kmemset(st, 0, sizeof(struct stat));
      st->st_mode = S_IFCHR | 0666;
      st->st_nlink = 1;
      st->st_rdev =
          (dev_t)((INPUT_EVDEV_MAJOR << 8) | (INPUT_EVDEV_MINOR_BASE + dev->index));
      st->st_blksize = 4096;
    }
    return 0;
  }

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
      id.bus = dev->bustype;
      id.vendor = dev->vendor;
      id.product = dev->product;
      id.version = dev->version;
      if (out != NULL) {
        kmemcpy(out, &id, sizeof(id));
      }
      return 0;
    }
    case 0x06: /* EVIOCGNAME */
      if (out != NULL && size > 0) {
        kstrncpy((char*)out, dev->name, size - 1);
        ((char*)out)[size - 1] = 0;
      }
      return 0;
    case 0x07: /* EVIOCGPHYS */
      if (out != NULL && size > 0) {
        kstrncpy((char*)out, dev->phys, size - 1);
        ((char*)out)[size - 1] = 0;
      }
      return 0;
    case 0x08: /* EVIOCGUNIQ */
      if (out != NULL && size > 0) {
        kstrncpy((char*)out, dev->uniq, size - 1);
        ((char*)out)[size - 1] = 0;
      }
      return 0;
    case 0x09: /* EVIOCGPROP：触摸板/指点杆/加速度计的判据 */
      in_copy_bits(out, size, dev->prop_bits,
                   sizeof(dev->prop_bits) / 4,
                   (INPUT_PROP_MAX + 8) / 8);
      return 0;
    case 0x0a: /* EVIOCGMTSLOTS */
      in_mt_slots(dev, out, size);
      return 0;
    case 0x18: /* EVIOCGKEY：当前按住的键 */
      in_copy_bits(out, size, dev->key_state, sizeof(dev->key_state) / 4,
                   (KEY_MAX + 8) / 8);
      return 0;
    case 0x1b: /* EVIOCGSW：当前开关状态 */
      in_copy_bits(out, size, dev->sw_state, sizeof(dev->sw_state) / 4,
                   (SW_MAX + 8) / 8);
      return 0;
    case 0x19: /* EVIOCGLED */
    case 0x1a: /* EVIOCGSND */
      if (out != NULL && size > 0) {
        kmemset(out, 0, size);
      }
      return 0;
    case 0x90: /* EVIOCGRAB */
    case 0x91: /* EVIOCREVOKE */
      return 0;
    case 0xa0: /* EVIOCSCLOCKID：本内核事件时间戳恒为单调钟 */
      return 0;
    default:
      break;
  }

  if (nr >= 0x20 && nr < 0x40) { /* EVIOCGBIT(ev, len) */
    u32 ev = nr - 0x20;
    switch (ev) {
      case EV_SYN: /* 特殊：EVIOCGBIT(0) 返回"支持哪些事件类型" */
        in_copy_bits(out, size, dev->ev_bits, 1, (EV_MAX + 8) / 8);
        break;
      case EV_KEY:
        in_copy_bits(out, size, dev->key_bits, sizeof(dev->key_bits) / 4,
                     (KEY_MAX + 8) / 8);
        break;
      case EV_REL:
        in_copy_bits(out, size, dev->rel_bits, sizeof(dev->rel_bits) / 4,
                     (REL_MAX + 8) / 8);
        break;
      case EV_ABS:
        in_copy_bits(out, size, dev->abs_bits, sizeof(dev->abs_bits) / 4,
                     (ABS_MAX + 8) / 8);
        break;
      case EV_SW:
        in_copy_bits(out, size, dev->sw_bits, sizeof(dev->sw_bits) / 4,
                     (SW_MAX + 8) / 8);
        break;
      default:
        if (out != NULL && size > 0) {
          kmemset(out, 0, size);
        }
        break;
    }
    return 0;
  }

  if (nr >= 0x40 && nr < 0x60) { /* EVIOCGABS(abs) */
    u32 abs = nr - 0x40;
    if (out != NULL && size > 0) {
      if (abs <= ABS_MAX && size >= sizeof(input_absinfo_t)) {
        kmemcpy(out, &dev->absinfo[abs], sizeof(input_absinfo_t));
      } else {
        kmemset(out, 0, size);
      }
    }
    return 0;
  }

  /* EVIOCSABS(abs)：libevdev 设置分辨率时会写回，接受即可（内核侧不改量程，
   * 否则用户态"设置"会被下一次 EVIOCGABS 打回原值）。 */
  if (nr >= 0xc0 && nr < 0xe0) {
    return 0;
  }

  return 0;
}

/* ---------------- 注册（建 /dev/input/eventN） ---------------- */
static void in_node_name(char* out, int n) {
  const char* p = "event";
  char tmp[8];
  int i = 0;
  int j = 0;

  while (*p) {
    out[j++] = *p++;
  }
  if (n <= 0) {
    tmp[i++] = '0';
  }
  while (n > 0) {
    tmp[i++] = (char)('0' + (n % 10));
    n /= 10;
  }
  while (i > 0) {
    out[j++] = tmp[--i];
  }
  out[j] = 0;
}

int input_register_device(input_dev_t* dev) {
  char nodename[16];
  vnode_t* node;
  int slot = -1;
  int i;

  if (dev == NULL) {
    return -1;
  }
  for (i = 0; i < INPUT_MAX_DEVICES; i++) {
    if (input_devs[i] == NULL) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    return -1;
  }

  if (input_dir == NULL) {
    input_dir = vfs_create_node("input", V_DIRECTORY);
    if (input_dir == NULL) {
      return -1;
    }
    /* 与 evdev 原来的做法一致：把 input 目录挂到 /dev 下 */
    vfs_mount(NULL, "/dev", input_dir);
  }

  dev->index = input_next_index++;
  dev->base.read = input_core_read;
  dev->base.ioctl = input_core_ioctl;
  dev->base.index = (u32)dev->index;
  device_add(&dev->base);

  in_node_name(nodename, dev->index);
  node = vfs_create_node((u8*)nodename, V_FILE);
  if (node == NULL) {
    return -1;
  }
  node->device = &dev->base;
  node->op = &device_operator;
  vfs_add_child(input_dir, node);

  input_devs[slot] = dev;
  return 0;
}
