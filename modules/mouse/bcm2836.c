/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "kernel/kernel.h"
#include "mouse.h"
#include "dev/devfs.h"
#include "usb/usb_mouse.h"

/* 本平台没有 PS/2 控制器（QEMU/raspi 的鼠标走 USB HID）。
 *
 * 这里不做任何硬件操作，只做格式转发：从 USB 鼠标驱动取包，再按调用方的
 * len 交付两种表示（与 pl050.c 的分派规则一致）：
 *   - xwin 的 xinput_poll：read(dev, buf, 3)，要**原始 PS/2 包**（相对位移），
 *     交给 xinput_ps2_mouse_data() 解析；
 *   - /dev/mouse（libgui）：read(fd, &mouse_data_t, 12)，要**绝对坐标事件**。
 *
 * 事件先落到本驱动的环形队列，两类读取者共用一份数据（读了就弹）。
 * 【必须能返回 0】xinput_poll 是 `while (read(...) >= 3)`：USB 侧没有新报告
 * 时必须返回 0，否则会死循环。 */

static mouse_device_t mouse_device;

static i32 mouse_clamp(i32 v) {
  if (v < 0) return 0;
  if (v > 0xffff) return 0xffff;
  return v;
}

/* 把 USB 侧待处理的报告搬进本驱动的环形事件队列（最多留 3 个未读事件，
 * 第 4 个槽用于区分队满/队空）。USB 队列空时取包接口会返回 0，循环即止。 */
static void mouse_pump(void) {
  for (;;) {
    if (((mouse_device.event_index + 1) & 0x3) == mouse_device.read_index) {
      return; /* 队满 */
    }

    u8 pkt[4];
    size_t n = usb_mouse_read_packet(pkt, sizeof(pkt));
    if (n < 3) {
      return; /* USB 侧暂无新报告 */
    }

    /* PS/2 包：byte0 含按键与符号位，byte1/byte2 为补码低 8 位 */
    i32 dx = (i32)(i8)pkt[1];
    i32 dy = (i32)(i8)pkt[2];
    /* PS/2 的 Y 向上为正，mouse_event_t 的 y 用屏幕坐标（向下为正） */
    mouse_device.x = (u32)mouse_clamp((i32)mouse_device.x + dx);
    mouse_device.y = (u32)mouse_clamp((i32)mouse_device.y - dy);
    mouse_device.btn_old = pkt[0] & 0x07;

    u32 idx = mouse_device.event_index;
    mouse_device.event_data[idx].sate = mouse_device.btn_old;
    mouse_device.event_data[idx].x = (i32)mouse_device.x;
    mouse_device.event_data[idx].y = (i32)mouse_device.y;
    mouse_device.packet_data[idx][0] = pkt[0];
    mouse_device.packet_data[idx][1] = pkt[1];
    mouse_device.packet_data[idx][2] = pkt[2];
    mouse_device.packet_data[idx][3] = (n > 3) ? pkt[3] : 0;
    mouse_device.event_index = (idx + 1) & 0x3;
  }
}

static size_t mouse_read(device_t* dev, void* buf, size_t len) {
  (void)dev;
  if (buf == NULL || len < 3) {
    return 0;
  }

  u32 idx = mouse_device.read_index;
  if (idx == mouse_device.event_index) {
    mouse_pump();
    idx = mouse_device.read_index;
    if (idx == mouse_device.event_index) {
      return 0; /* 没有新数据 */
    }
  }

  if (len >= sizeof(mouse_event_t)) {
    *(mouse_event_t*)buf = mouse_device.event_data[idx];
    mouse_device.read_index = (idx + 1) & 0x3;
    return sizeof(mouse_event_t);
  }

  /* 3 字节原始包（PS/2：byte0=按键+符号位，byte1=X，byte2=Y） */
  u8* out = (u8*)buf;
  out[0] = mouse_device.packet_data[idx][0];
  out[1] = mouse_device.packet_data[idx][1];
  out[2] = mouse_device.packet_data[idx][2];
  mouse_device.read_index = (idx + 1) & 0x3;
  return 3;
}

int mouse_init(void) {
  device_t* dev = kmalloc(sizeof(device_t), KERNEL_TYPE);
  if (dev == NULL) {
    return -1;
  }
  kmemset(dev, 0, sizeof(device_t));
  kmemset(&mouse_device, 0, sizeof(mouse_device));

  dev->name = "mouse";
  dev->read = mouse_read;
  dev->id = DEVICE_MOUSE;
  dev->type = DEVICE_TYPE_CHAR;
  dev->data = &mouse_device;
  device_add(dev);

  {
    device_t* mouse_dev = device_find(DEVICE_MOUSE);
    if (mouse_dev != NULL) {
      vnode_t* mouse = vfs_create_node("mouse", V_FILE);
      vfs_mount(NULL, "/dev", mouse);
      mouse->device = mouse_dev;
      mouse->op = &device_operator;
    } else {
      kprintf("dev mouse not found\n");
    }
  }

  log_info("mouse: report from usb hid\n");
  return 0;
}

void mouse_exit(void) { kprintf("mouse exit\n"); }

module_t mouse_module = {
    .name = "mouse", .init = mouse_init, .exit = mouse_exit};
