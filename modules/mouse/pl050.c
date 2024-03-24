/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "dev/devfs.h"
#include "gpio.h"
#include "kernel/kernel.h"
#include "mouse.h"

#define VERSATILEPB_PL050_KBD 0x10006000
#define SIC_IRQ_VIC_BIT (1 << 31)
#define KMI0_IRQ_SIC_BIT (1 << 3)
#define KMI1_IRQ_SIC_BIT (1 << 4)

#define KCNTL 0x00  // 7-6- 5(0=AT)4=RxIntEn 3=TxIntEn
#define KSTAT 0x04  // 7-6=TxE 5=TxBusy 4=RXFull 3=RxBusy
#define KDATA 0x08  // data register;
#define KCLK 0x0C   // clock divisor register; (not used)
#define KISTA 0x10  // interrupt status register;(not used)

// *(volatile u32*)(VERSATILEPB_PL050_KBD + KCNTL) = 0x14;
// *(volatile u32*)(VERSATILEPB_PL050_KBD + KCLK) = 8;
// u8 scode = *(VERSATILEPB_PL050_KBD + KDATA);

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

static size_t read(device_t* dev, void* buf, size_t len) {
  u32 ret = len;

  if (mouse_device.event_index < 0) {
    return 0;
  }

  // kprintf("read %x %d %d\n", buf, len, mouse_device.event_index);

  mouse_event_t* data = buf;

  *data = mouse_device.event_data[mouse_device.event_index];
  if (mouse_device.event_index < 0) {
    mouse_device.event_index = 0;
  }
  return ret;
}

void* mouse_handler(interrupt_context_t* ic) {
  u32 read_count = 0;
  u8 state = 0;
  u32 rx, ry, rz;
  u8 btndown, btnup, btn;

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

      if (btndown == 0 && rz != 0) {
        btndown = 8;  // scroll wheel
        rx = rz;
      }

      mouse_device.x += rx;
      mouse_device.y -= ry;

      // kprintf("mouse %d,%d btn= %x\n", mouse_device.x, mouse_device.y, btndown);

      mouse_device.event_data[mouse_device.event_index].x = mouse_device.x;
      mouse_device.event_data[mouse_device.event_index].y = mouse_device.y;
      mouse_device.event_data[mouse_device.event_index].sate = btndown;

      mouse_device.event_index = (mouse_device.event_index + 1) & 0x3;
    }
    state = io_read8(MOUSE_BASE + MOUSE_IIR);
  }

  return NULL;
}

int mouse_init(void) {
  device_t* dev = kmalloc(sizeof(device_t), DEFAULT_TYPE);
  dev->name = "mouse";
  dev->read = read;
  dev->id = DEVICE_MOUSE;
  dev->type = DEVICE_TYPE_CHAR;
  dev->data = &mouse_device;

  mouse_device.event_index = 0;
  mouse_device.x = 0;
  mouse_device.y = 0;

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

  exception_regist(EX_MOUSE, mouse_handler);

  // mouse init
  page_map(MOUSE_BASE, MOUSE_BASE, 0);

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

  u32 pic = io_read32(SIC_BASE + SIC_INT_ENABLE);
  pic |= 1 << 4;  // 4 on secondary controller KMI 1
  io_write32(SIC_BASE + SIC_INT_ENABLE, pic);

  return 0;
}

void mouse_exit(void) { kprintf("mouse exit\n"); }

module_t mouse_module = {
    .name = "mouse", .init = mouse_init, .exit = mouse_exit};
