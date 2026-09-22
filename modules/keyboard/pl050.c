/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "dev/devfs.h"
#include "gpio.h"
#include "kernel/kernel.h"
#include "kernel/irq.h"
#include "keyboard.h"

#define VERSATILEPB_PL050_KBD 0x10006000
#define SIC_IRQ_VIC_BIT (1 << 31)
#define KMI0_IRQ_SIC_BIT (1 << 3)
#define KMI1_IRQ_SIC_BIT (1 << 4)

#define KCNTL 0x00  // 7-6- 5(0=AT)4=RxIntEn 3=TxIntEn
#define KSTAT 0x04  // 7-6=TxE 5=TxBusy 4=RXFull 3=RxBusy
#define KDATA 0x08  // data register;
#define KCLK 0x0C   // clock divisor register; (not used)
#define KISTA 0x10  // interrupt status register;(not used)

#define LSHIFT 18
#define RSHIT 89
#define ENTER 90
#define KEY_RELEASE_PREFIX 240

keyboard_device_t keyboard_device;

static size_t read(device_t* dev, void* buf, size_t len) {
  u32 ret = 0;
  if (keyboard_device.scan_code_index > 0) {
    /* FIFO：返回【最旧】字节，再整体前移。原来返回 [index-1]（最新）并把
     * 最旧的覆盖掉 ⇒ 0xE0 扩展前缀被丢、字节重复，方向键因此失效。 */
    kstrncpy(buf, &keyboard_device.scan_code_buffer[0], 1);
    for (int i = 0; i + 1 < keyboard_device.scan_code_index; i++) {
      keyboard_device.scan_code_buffer[i] =
          keyboard_device.scan_code_buffer[i + 1];
    }
    keyboard_device.scan_code_index--;
    ret = 1;
  }
  return ret;
}

static int keyboard_irq_handler(u32 irq, void* arg) {
  // Scan Code Set 2
  u32 scan_code = io_read8(VERSATILEPB_PL050_KBD + KDATA);
  if (keyboard_device.scan_code_index > MAX_CHARCODE_BUFFER) {
    keyboard_device.scan_code_index = 0;
    log_warn("key buffer is full\n");
  }

  /* 0xF0 是 Set 2 的释放前缀：只置标志，本身不入 buffer（否则上层会把它
   * 当成一个键码解析）。0xE0 扩展前缀保留在流里，由 xinput 解析。 */
  if (scan_code == KEY_RELEASE_PREFIX) {
    keyboard_device.key_release = 1;
    return 1;
  }

  if (keyboard_device.key_release) {
    keyboard_device.key_release = 0;
    scan_code |= 0x80;
  }

  if (scan_code == LSHIFT || scan_code == RSHIT) {
    // shift_on = key_release ? FALSE : TRUE;
    // return;
  }
  kprintf("key press %d code %x\n", keyboard_device.key_release, scan_code);

  keyboard_device.scan_code_buffer[keyboard_device.scan_code_index++] =
      scan_code;

  return 1;
}

int keyboard_init(void) {
  device_t* dev = kmalloc(sizeof(device_t), DEFAULT_TYPE);
  dev->name = "keyboard";
  dev->read = read;
  dev->id = DEVICE_KEYBOARD;
  dev->type = DEVICE_TYPE_CHAR;
  dev->data = &keyboard_device;

  keyboard_device.scan_code_index = 0;

  device_add(dev);

  // keyboard
  device_t* keyboard_dev = device_find(DEVICE_KEYBOARD);
  if (keyboard_dev != NULL) {
    vnode_t* keyboard = vfs_create_node("keyboard", V_FILE);
    vfs_mount(NULL, "/dev", keyboard);
    keyboard->device = keyboard_dev;
    keyboard->op = &device_operator;
  } else {
    kprintf("dev keyboard not found\n");
  }

#ifdef VERSATILE_USE_IRQ_CHIP
  irq_register(IRQ_SIC_KBD, keyboard_irq_handler, NULL, "kbd", 0);
#else
  exception_regist(EX_KEYBOARD, keyboard_handler);
#endif

  // keyboard init
  page_map(VERSATILEPB_PL050_KBD, VERSATILEPB_PL050_KBD, PAGE_DEV);

  *(volatile u32*)(VERSATILEPB_PL050_KBD + KCNTL) = 0x14;
  *(volatile u32*)(VERSATILEPB_PL050_KBD + KCLK) = 8;

#ifdef VERSATILE_USE_IRQ_CHIP
  irq_enable(IRQ_SIC_KBD);
#else
  u32 pic = io_read32(SIC_BASE + SIC_INT_ENABLE);
  pic |= 1 << 3;  // 4 on secondary controller KMI 1
  io_write32(SIC_BASE + SIC_INT_ENABLE, pic);
#endif

  return 0;
}

void keyboard_exit(void) { kprintf("keyboard exit\n"); }

module_t keyboard_module = {
    .name = "keyboard", .init = keyboard_init, .exit = keyboard_exit};
