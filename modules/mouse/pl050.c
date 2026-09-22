/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "dev/devfs.h"
#include "gpio.h"
#include "kernel/kernel.h"
#include "kernel/irq.h"
#include "mouse.h"


#define VERSATILEPB_PL050_MOUSE 0x10007000

#define MOUSE_CR 0x00
#define MOUSE_STAT 0x04
#define MOUSE_DATA 0x08
#define MOUSE_CLKDIV 0x0C
#define MOUSE_IIR 0x10

#define MOUSE_CR_TYPE (1 << 5)
#define MOUSE_CR_RXINTREN (1 << 4)
#define MOUSE_CR_TXINTREN (1 << 3)
#define MOUSE_CR_EN (1 << 2)
#define MOUSE_CR_FD (1 << 1)
#define MOUSE_CR_FC (1 << 0)

#define MOUSE_STAT_TXEMPTY (1 << 6)
#define MOUSE_STAT_TXBUSY (1 << 5)
#define MOUSE_STAT_RXFULL (1 << 4)
#define MOUSE_STAT_RXBUSY (1 << 3)
#define MOUSE_STAT_RXPARITY (1 << 2)
#define MOUSE_STAT_IC (1 << 1)
#define MOUSE_STAT_ID (1 << 0)

#define MOUSE_IIR_TXINTR (1 << 1)
#define MOUSE_IIR_RXINTR (1 << 0)

#define MOUSE_BASE VERSATILEPB_PL050_MOUSE

static inline int32_t kmi_write(uint8_t data) {
  int32_t timeout = 1000;

  while ((io_read8(MOUSE_BASE + MOUSE_STAT) & MOUSE_STAT_TXEMPTY) == 0 &&
         timeout--)
    ;
  if (timeout) {
    io_write8(MOUSE_BASE + MOUSE_DATA, data);
    while ((io_read8(MOUSE_BASE + MOUSE_STAT) & MOUSE_STAT_RXFULL) == 0)
      ;
    if (io_read8(MOUSE_BASE + MOUSE_DATA) == 0xfa)
      return 1;
    else
      return 0;
  }
  return 0;
}

static inline int32_t kmi_read(uint8_t* data) {
  if ((io_read8(MOUSE_BASE + MOUSE_STAT) & MOUSE_STAT_RXFULL)) {
    *data = io_read8(MOUSE_BASE + MOUSE_DATA);
    return 1;
  }
  return 0;
}

mouse_device_t mouse_device;

/* 【按 len 分派两种表示】同一驱动被两类调用方读取：
 *   - xwin 的 xinput_poll：`read(dev, data4, 3)`，期望 **原始 PS/2 包**，
 *     交给 xinput_ps2_mouse_data() 解析（相对位移）；
 *   - /dev/mouse（libgui event_read_mouse 的非 xwin 路径）：len=12，
 *     期望 **绝对坐标事件**（mouse_event_t）。
 * 两者数据都来自中断里组好的包，这里按 len 分派即可，双方代码都不用改。
 *
 * 【必须能返回 0】调用方是 `while (read(...) >= 3)`：返回非 0 必须对应真实数据，
 * 否则会死循环 —— 原来 `u32 ret = len; ... return ret;` 无条件返回 len，导致
 * xwin_get_event 系统调用永不返回（实测 infones 卡在 xinput_ps2_mouse_data 里
 * 反复执行，最终内存被冲垮、pop {fp,pc} 跳到异常向量表）；而且 12 字节写进
 * 4 字节缓冲还会踩掉调用者的 fp/lr。 */
static size_t read(device_t* dev, void* buf, size_t len) {
  (void)dev;
  if (buf == NULL || len < 3) {
    return 0;
  }
  u32 idx = mouse_device.read_index;
  if (idx == mouse_device.event_index) {
    return 0; /* 没有新数据 */
  }
  if (len >= sizeof(mouse_event_t)) {
    *(mouse_event_t*)buf = mouse_device.event_data[idx];
    mouse_device.read_index = (idx + 1) & 0x3;
    return sizeof(mouse_event_t);
  }
  /* 3 字节原始包（PS/2：byte0=按键+符号位，byte1=X，byte2=Y） */
  u8* out = buf;
  out[0] = mouse_device.packet_data[idx][0];
  out[1] = mouse_device.packet_data[idx][1];
  out[2] = mouse_device.packet_data[idx][2];
  mouse_device.read_index = (idx + 1) & 0x3;
  return 3;
}

static int mouse_irq_handler(u32 irq, void* arg) {
  u32 read_count = 0;
  u8 state = 0;
  u32 rx, ry, rz;
  u8 btndown, btnup, btn;
  u8 btn_state = 0;

  state = io_read8(MOUSE_BASE + MOUSE_IIR);

  for (; state & MOUSE_IIR_RXINTR;) {
    u8 data = io_read8(MOUSE_BASE + MOUSE_DATA);
    mouse_device.packet[mouse_device.packet_index] = data;

    mouse_device.packet_index = (mouse_device.packet_index + 1) & 0x3;

    if (mouse_device.packet_index == 0) {
      btn = mouse_device.packet[0] & 0x7;

      btndown = (btn ^ mouse_device.btn_old) & btn;
      btnup = (btn ^ mouse_device.btn_old) & mouse_device.btn_old;
      mouse_device.btn_old = btn;

      btndown = (btn ^ mouse_device.btn_old) & btn;
      btnup = (btn ^ mouse_device.btn_old) & mouse_device.btn_old;
      mouse_device.btn_old = btn;

      if (mouse_device.packet[0] & 0x10) {
        rx = (int8_t)(0xffffff00 | mouse_device.packet[1]);  // nagtive
      } else {
        rx = (int8_t)mouse_device.packet[1];
      }
      if (mouse_device.packet[0] & 0x20) {
        ry = -(int8_t)(0xffffff00 | mouse_device.packet[2]);  // nagtive
      } else {
        ry = -(int8_t)mouse_device.packet[2];
      }

      rz = (int8_t)(mouse_device.packet[3] & 0xf);
      if (rz == 0xf) rz = -1;

      btndown = (btndown << 1 | btnup);

      btn_state = btn;

      // kprintf("mouse %d,%d btn= %x %x\n", mouse_device.x, mouse_device.y,
      //         btndown, btnup);

      if (btndown == 0 && rz != 0) {
        btndown = 8;  // scroll wheel
        rx = rz;
      }

      mouse_device.x += rx;
      mouse_device.y += ry;

      /* 环形缓冲满（已有 3 个未读事件）时丢弃最新事件：不能覆盖未读事件，
       * 否则读端 read_index 会读到错位内容 */
      if (((mouse_device.event_index + 1) & 0x3) != mouse_device.read_index) {
        mouse_device.event_data[mouse_device.event_index].x = mouse_device.x;
        mouse_device.event_data[mouse_device.event_index].y = mouse_device.y;
        mouse_device.event_data[mouse_device.event_index].sate = btn_state;
        /* 原始包也留一份：xwin 侧按 len=3 读它（见 read 的分派说明） */
        mouse_device.packet_data[mouse_device.event_index][0] =
            mouse_device.packet[0];
        mouse_device.packet_data[mouse_device.event_index][1] =
            mouse_device.packet[1];
        mouse_device.packet_data[mouse_device.event_index][2] =
            mouse_device.packet[2];
        mouse_device.packet_data[mouse_device.event_index][3] =
            mouse_device.packet[3];

        mouse_device.event_index = (mouse_device.event_index + 1) & 0x3;
      }
    }
    state = io_read8(MOUSE_BASE + MOUSE_IIR);
  }

  return 1;
}

int mouse_init(void) {
  device_t* dev = kmalloc(sizeof(device_t), DEFAULT_TYPE);
  dev->name = "mouse";
  dev->read = read;
  dev->id = DEVICE_MOUSE;
  dev->type = DEVICE_TYPE_CHAR;
  dev->data = &mouse_device;

  mouse_device.event_index = 0;
  mouse_device.read_index = 0;
  mouse_device.x = 0;
  mouse_device.y = 0;
  mouse_device.btn_old = 0;

  device_add(dev);

  // mouse
  device_t* mouse_dev = device_find(DEVICE_MOUSE);
  if (mouse_dev != NULL) {
    vnode_t* mouse = vfs_create_node("mouse", V_FILE);
    vfs_mount(NULL, "/dev", mouse);
    mouse->device = mouse_dev;
    mouse->op = &device_operator;
  } else {
    kprintf("dev mouse not found\n");
  }

#ifdef VERSATILE_USE_IRQ_CHIP
  irq_register(IRQ_SIC_MOUSE, mouse_irq_handler, NULL, "mouse", 0);
#else
  exception_regist(EX_MOUSE, mouse_handler);
#endif

  // mouse init
  page_map(MOUSE_BASE, MOUSE_BASE, PAGE_DEV);

  uint8_t data;
  uint32_t divisor = 1000;
  io_write8(MOUSE_BASE + MOUSE_CLKDIV, divisor);
  io_write8(MOUSE_BASE + MOUSE_CR, MOUSE_CR_EN);

  // reset mouse, and wait ack and pass/fail code
  if (!kmi_write(0xff)) return -1;
  if (!kmi_read(&data)) return -1;
  if (data != 0xaa) return -1;

  // enable scroll wheel
  kmi_write(0xf3);
  kmi_write(200);

  kmi_write(0xf3);
  kmi_write(100);

  kmi_write(0xf3);
  kmi_write(80);

  kmi_write(0xf2);
  kmi_read(&data);
  kmi_read(&data);

  // set sample rate, 100 samples/sec
  kmi_write(0xf3);
  kmi_write(100);

  // set resolution, 4 counts per mm, 1:1 scaling
  kmi_write(0xe8);
  kmi_write(0x02);
  kmi_write(0xe6);
  // enable data reporting
  kmi_write(0xf4);
  // clear a receive buffer
  kmi_read(&data);
  kmi_read(&data);
  kmi_read(&data);
  kmi_read(&data);

  /* re-enables mouse */
  io_write8(MOUSE_BASE + MOUSE_CR, MOUSE_CR_EN | MOUSE_CR_RXINTREN);

#ifdef VERSATILE_USE_IRQ_CHIP
  irq_enable(IRQ_SIC_MOUSE);
#else
  u32 pic = io_read32(SIC_BASE + SIC_INT_ENABLE);
  pic |= 1 << 4;  // 4 on secondary controller KMI 1
  io_write32(SIC_BASE + SIC_INT_ENABLE, pic);
#endif

  return 0;
}

void mouse_exit(void) { kprintf("mouse exit\n"); }

module_t mouse_module = {
    .name = "mouse", .init = mouse_init, .exit = mouse_exit};
