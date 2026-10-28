/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 *
 * 内核 input 设备核心：把"一块输入设备"的能力位图、absinfo、属性位和事件
 * 队列统一起来，并按 Linux evdev 的语义导出 /dev/input/eventN。
 *
 * 为什么需要它：用户态的输入栈（libevdev / libinput / Xorg 的 evdev 驱动）
 * 不看驱动内部状态，只看 evdev 节点上的 ioctl 结果：
 *   EVIOCGBIT(0)            → 支持哪些事件类型
 *   EVIOCGBIT(EV_KEY/REL/ABS)→ 具体支持哪些码
 *   EVIOCGPROP              → 是触摸板（INPUT_PROP_POINTER/BUTTONPAD）还是鼠标
 *   EVIOCGABS               → 绝对轴量程，触摸板/触摸屏必需
 *   read()                  → 带时间戳的 struct input_event
 * 缺其中任何一项，libinput 都会把设备判成"不认识"而丢掉。所以驱动只需声明
 * 能力，ioctl 与节点创建由本文件统一提供。
 ********************************************************************/
#ifndef INPUT_CORE_H
#define INPUT_CORE_H

#include "kernel/device.h"
#include "kernel/kernel.h"

/* ---------------- Linux input uapi 常量（本内核用到的子集） ---------------- */
#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_REL 0x02
#define EV_ABS 0x03
#define EV_MSC 0x04
#define EV_SW 0x05
#define EV_LED 0x11
#define EV_SND 0x12
#define EV_REP 0x14
#define EV_MAX 0x1f

#define SYN_REPORT 0x00
#define SYN_CONFIG 0x01
#define SYN_DROPPED 0x03

/* 相对轴 */
#define REL_X 0x00
#define REL_Y 0x01
#define REL_Z 0x02
#define REL_RX 0x03
#define REL_RY 0x04
#define REL_RZ 0x05
#define REL_HWHEEL 0x06
#define REL_DIAL 0x07
#define REL_WHEEL 0x08
#define REL_MAX 0x0f

/* 绝对轴 */
#define ABS_X 0x00
#define ABS_Y 0x01
#define ABS_Z 0x02
#define ABS_RX 0x03
#define ABS_RY 0x04
#define ABS_RZ 0x05
#define ABS_THROTTLE 0x06
#define ABS_RUDDER 0x07
#define ABS_WHEEL 0x08
#define ABS_GAS 0x09
#define ABS_BRAKE 0x0a
#define ABS_HAT0X 0x10
#define ABS_HAT0Y 0x11
#define ABS_HAT1X 0x12
#define ABS_HAT1Y 0x13
#define ABS_HAT2X 0x14
#define ABS_HAT2Y 0x15
#define ABS_HAT3X 0x16
#define ABS_HAT3Y 0x17
#define ABS_PRESSURE 0x18
#define ABS_DISTANCE 0x19
#define ABS_TILT_X 0x1a
#define ABS_TILT_Y 0x1b
#define ABS_TOOL_WIDTH 0x1c
#define ABS_MISC 0x28
#define ABS_MT_SLOT 0x2f
#define ABS_MT_TOUCH_MAJOR 0x30
#define ABS_MT_TOUCH_MINOR 0x31
#define ABS_MT_WIDTH_MAJOR 0x32
#define ABS_MT_WIDTH_MINOR 0x33
#define ABS_MT_ORIENTATION 0x34
#define ABS_MT_POSITION_X 0x35
#define ABS_MT_POSITION_Y 0x36
#define ABS_MT_TOOL_TYPE 0x37
#define ABS_MT_TRACKING_ID 0x39
#define ABS_MT_PRESSURE 0x3a
#define ABS_MT_DISTANCE 0x3b
#define ABS_MAX 0x3f

/* 按键/按钮 */
#define KEY_MAX 0x2ff
#define BTN_MISC 0x100
#define BTN_0 0x100
#define BTN_1 0x101
#define BTN_2 0x102
#define BTN_3 0x103
#define BTN_4 0x104
#define BTN_5 0x105
#define BTN_6 0x106
#define BTN_7 0x107
#define BTN_8 0x108
#define BTN_9 0x109
#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111
#define BTN_MIDDLE 0x112
#define BTN_SIDE 0x113
#define BTN_EXTRA 0x114
#define BTN_FORWARD 0x115
#define BTN_BACK 0x116
#define BTN_TASK 0x117
#define BTN_TOUCH 0x14a
#define BTN_STYLUS 0x14b
#define BTN_STYLUS2 0x14c
#define BTN_TOOL_PEN 0x140
#define BTN_TOOL_RUBBER 0x141
#define BTN_TOOL_FINGER 0x145
#define BTN_TRIGGER 0x120
#define BTN_JOYSTICK 0x120
#define BTN_THUMB 0x121

/* 开关（盖子/平板模式） */
#define SW_LID 0x00
#define SW_TABLET_MODE 0x01
#define SW_MAX 0x10

/* 设备属性 */
#define INPUT_PROP_POINTER 0x00
#define INPUT_PROP_DIRECT 0x01
#define INPUT_PROP_BUTTONPAD 0x02
#define INPUT_PROP_SEMI_MT 0x03
#define INPUT_PROP_POINTING_STICK 0x05
#define INPUT_PROP_ACCELEROMETER 0x06
#define INPUT_PROP_MAX 0x1f

/* 总线类型（EVIOCGID 的 bustype） */
#define BUS_PCI 0x01
#define BUS_USB 0x03
#define BUS_BLUETOOTH 0x05
#define BUS_VIRTUAL 0x06
#define BUS_I8042 0x11
#define BUS_I2C 0x18
#define BUS_SPI 0x1c

/* ---------------- 事件与能力结构 ---------------- */
/* aarch64 的 struct input_event：{ i64 sec; i64 usec; u16 type; u16 code; i32 value; } */
typedef struct input_event {
  i64 sec;
  i64 usec;
  u16 type;
  u16 code;
  i32 value;
} input_event_t;

typedef struct input_absinfo {
  i32 value;
  i32 minimum;
  i32 maximum;
  i32 fuzz;
  i32 flat;
  i32 resolution;
} input_absinfo_t;

#define INPUT_MAX_DEVICES 8
#define INPUT_QUEUE_SIZE 128
#define INPUT_NAME_MAX 64

/* Linux input 设备号：主 13，eventN 次 64+N。必须与 posix 层 sysfs 垫片
 * （/sys/dev/char/13:64+N）以及 udev 的 devnum 反查保持一致。 */
#define INPUT_EVDEV_MAJOR 13
#define INPUT_EVDEV_MINOR_BASE 64

#define INPUT_BITS(words) ((words) * 32)

typedef struct input_dev {
  device_t base; /* 必须首字段：device_t* 与 input_dev_t* 可互转 */

  char name[INPUT_NAME_MAX];
  char phys[32];
  char uniq[32];

  u16 bustype;
  u16 vendor;
  u16 product;
  u16 version;

  /* 能力位图（下标即码值） */
  u32 ev_bits[1];                       /* EV_MAX 0x1f → 32 bit */
  u32 key_bits[(KEY_MAX + 32) / 32];    /* 0x2ff → 24 words */
  u32 rel_bits[(REL_MAX + 32) / 32];
  u32 abs_bits[(ABS_MAX + 32) / 32];
  u32 sw_bits[(SW_MAX + 32) / 32];
  u32 prop_bits[(INPUT_PROP_MAX + 32) / 32];

  /* 当前按键/开关状态（EVIOCGKEY / EVIOCGSW） */
  u32 key_state[(KEY_MAX + 32) / 32];
  u32 sw_state[(SW_MAX + 32) / 32];

  input_absinfo_t absinfo[ABS_MAX + 1];

  /* 事件队列：驱动 report 进 tail，用户 read 从 head 取 */
  input_event_t queue[INPUT_QUEUE_SIZE];
  u32 head;
  u32 tail;

  int index;                     /* eventN 的 N */
  u32 pumping;                   /* 抽包重入保护（由驱动使用） */
  void (*pump)(struct input_dev*); /* read/就绪判定前调用：从硬件拉数据 */
} input_dev_t;

/* ---------------- 驱动侧 API ---------------- */
/* 分配并清零一块设备（不注册）。失败返回 NULL。 */
input_dev_t* input_alloc_device(const char* name);
/* 身份信息（EVIOCGID / udev 的 PRODUCT 属性来源） */
void input_set_ids(input_dev_t* dev, u16 bustype, u16 vendor, u16 product,
                   u16 version);
/* 声明能力：先 set_type，再 set_code */
void input_set_type(input_dev_t* dev, u32 type);
void input_set_code(input_dev_t* dev, u32 type, u32 code);
/* 绝对轴量程（EVIOCGABS 的来源）；未声明的轴按 0 返回 */
void input_set_absinfo(input_dev_t* dev, u32 code, i32 minimum, i32 maximum,
                       i32 fuzz, i32 flat, i32 resolution);
void input_set_prop(input_dev_t* dev, u32 prop);
/* 注册：分配 /dev/input/eventN 节点与 device_t。返回 0 成功。 */
int input_register_device(input_dev_t* dev);
/* 上报事件：type/code/value 按 Linux 语义；EV_SYN/SYN_REPORT 会结束一帧 */
void input_report(input_dev_t* dev, u32 type, u32 code, i32 value);
void input_sync(input_dev_t* dev);
/* 还有未读事件？供 poll/select/epoll 的就绪判定（会先 pump 一次） */
int input_has_event(input_dev_t* dev);
/* 给 posix 层弱符号 evdev_poll_ready 用：device_t* → 是否有事件 */
int input_poll_ready(void* arg);

#endif
