/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 *
 * 相对指针设备（鼠标）：把平台鼠标驱动的 PS/2/USB 报告展开成 Linux input
 * 事件流，通过 input_core 注册成 /dev/input/eventN。
 *
 * 设备的"能力"（EV_KEY/EV_REL 位图、按键码、身份信息）在这里声明，
 * 事件队列、ioctl、节点创建由 input_core.c 统一提供——用户态
 * （libevdev / libinput / Xorg 的 evdev 驱动）只通过这些 ioctl 认识设备。
 *
 * 数据源是 platform 鼠标驱动（device_find(DEVICE_MOUSE) 的 read，返回
 * 三字节/四字节报告），因此 /dev/mouse 与 /dev/input/eventN 共用同一份输入。
 ********************************************************************/
#include "input_core.h"

#include "dev/devfs.h"
#include "kernel/kernel.h"

#define EVDEV_NAME "YiYiYa Virtual Mouse"
#define EVDEV_PHYS "yiyiya/input0"

/* 一次 pump 最多搬运多少份硬件报告（1 份 = 1~5 个 input 事件）。
 * 只是防"端点一直有数据"时霸占 CPU，正常一帧也就 1~3 份。 */
#define EVDEV_PUMP_MAX 16

static input_dev_t* evdev_mouse;
static u32 evdev_btn_old;

/* 从平台鼠标驱动取一份报告并展开成 input 事件序列。
 * 【没有变化就不投递】QEMU 的 HID 空闲时会周期性上报全零报告，
 * 若照单全收，用户态会空转（每 10ms 一次 read）。重入保护由 input_core
 * 的 in_pump() 负责（read 与就绪判定会从不同线程进来）。 */
static void evdev_pump(input_dev_t* dev) {
  device_t* mouse = device_find(DEVICE_MOUSE);
  /* 一次 pump 把硬件侧积压的报告全部展开成事件（上限 EVDEV_PUMP_MAX 包）：
   * 用户态一次 read 就能拿到成批的 REL_X/REL_Y/SYN。
   * 【为什么必须批量】旧实现每次只取一个包，而 poll/epoll 对本类设备是按
   * SYS_POLLED_FD_RECHECK_MS(8ms) 切片重查的，等于每 8ms 只吐一包（≈41 包/秒），
   * 快速拖动时用户态看到的是"一跳一跳"的光标，且 1 包 = 3 个事件要分 3 次
   * read 才取完，延迟再翻倍。 */
  int guard;

  if (mouse == NULL || mouse->read == NULL) {
    return;
  }

  for (guard = 0; guard < EVDEV_PUMP_MAX; guard++) {
    u8 pkt[4];
    size_t n;
    i32 dx;
    i32 dy;
    u32 btn;

    n = mouse->read(mouse, pkt, sizeof(pkt));
    if (n < 3) {
      return; /* 硬件侧暂无新报告 */
    }

    dx = (i32)(i8)pkt[1];
    dy = (i32)(i8)pkt[2];
    btn = pkt[0] & 0x07;

    /* 空闲时的全零报告：没有变化就不投递，但要继续看后面还有没有真事件 */
    if (dx == 0 && dy == 0 && btn == evdev_btn_old) {
      continue;
    }

    if (dx != 0) {
      input_report(dev, EV_REL, REL_X, dx);
    }
    if (dy != 0) {
      /* 【不要取反】usb_mouse_read_packet 交出的 byte2 就是 USB HID 报告的
       * dy，正向已经是屏幕方向（向下为正），与 EV_REL/REL_Y 的约定一致。 */
      input_report(dev, EV_REL, REL_Y, dy);
    }
    if ((btn & 0x01) != (evdev_btn_old & 0x01)) {
      input_report(dev, EV_KEY, BTN_LEFT, (btn & 0x01) ? 1 : 0);
    }
    if ((btn & 0x02) != (evdev_btn_old & 0x02)) {
      input_report(dev, EV_KEY, BTN_RIGHT, (btn & 0x02) ? 1 : 0);
    }
    if ((btn & 0x04) != (evdev_btn_old & 0x04)) {
      input_report(dev, EV_KEY, BTN_MIDDLE, (btn & 0x04) ? 1 : 0);
    }
    evdev_btn_old = btn;

    input_sync(dev);
  }
}

/* posix 层（sys_fs 的就绪判定）以弱符号调用：设备是 DEVICE_EVDEV 时问这里
 * "现在可不可读"。arg 是 device_t*，与 input_dev_t* 同地址（base 首字段）。 */
int evdev_poll_ready(void* arg) {
  return input_poll_ready(arg);
}

/* 模块 init 是 void (*)()：失败只能打日志返回，没有错误码通道。 */
static void evdev_init(void) {
  input_dev_t* dev;

  dev = input_alloc_device(EVDEV_NAME);
  if (dev == NULL) {
    log_error("evdev: alloc device failed\n");
    return;
  }
  kstrncpy(dev->phys, EVDEV_PHYS, sizeof(dev->phys) - 1);

  /* 身份：QEMU/真机的通用 USB 鼠标 */
  input_set_ids(dev, BUS_USB, 0x0627, 0x0001, 0x0111);

  /* 能力位：EV_SYN + EV_KEY(BTN_LEFT/RIGHT/MIDDLE) + EV_REL(X/Y/WHEEL) */
  input_set_type(dev, EV_SYN);
  input_set_type(dev, EV_KEY);
  input_set_type(dev, EV_REL);
  input_set_code(dev, EV_KEY, BTN_LEFT);
  input_set_code(dev, EV_KEY, BTN_RIGHT);
  input_set_code(dev, EV_KEY, BTN_MIDDLE);
  input_set_code(dev, EV_REL, REL_X);
  input_set_code(dev, EV_REL, REL_Y);
  input_set_code(dev, EV_REL, REL_WHEEL);

  dev->pump = evdev_pump;

  evdev_btn_old = 0;
  if (input_register_device(dev) < 0) {
    log_error("evdev: register device failed\n");
    kfree(dev);
    return;
  }
  evdev_mouse = dev;
  log_info("evdev: %s ready as event%d\n", EVDEV_NAME, dev->index);
}

void evdev_exit(void) { log_info("evdev exit\n"); }

module_t input_module = {
    .name = "input", .init = evdev_init, .exit = evdev_exit};
