/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "dev/devfs.h"
#include "gpio.h"
#include "gpio/sunxi-gpio.h"
#include "i2c/i2c.h"
#include "kernel/kernel.h"
#include "keyboard.h"

#define PCAL6416A_I2C_ADDR 0x20
#define PCAL9539A_I2C_ADDR 0x76

#define PCAL6416A_INPUT 0x00          /* Input port [RO] */
#define PCAL6416A_DAT_OUT 0x02        /* GPIO DATA OUT [R/W] */
#define PCAL6416A_POLARITY 0x04       /* Polarity Inversion port [R/W] */
#define PCAL6416A_CONFIG 0x06         /* Configuration port [R/W] */
#define PCAL6416A_DRIVE0 0x40         /* Output drive strength Port0 [R/W] */
#define PCAL6416A_DRIVE1 0x42         /* Output drive strength Port1 [R/W] */
#define PCAL6416A_INPUT_LATCH 0x44    /* Port0 Input latch [R/W] */
#define PCAL6416A_EN_PULLUPDOWN 0x46  /* Port0 Pull-up/Pull-down enbl [R/W] */
#define PCAL6416A_SEL_PULLUPDOWN 0x48 /* Port0 Pull-up/Pull-down slct [R/W] */
#define PCAL6416A_INT_MASK 0x4A       /* Interrupt mask [R/W] */
#define PCAL6416A_INT_STATUS 0x4C     /* Interrupt status [RO] */
#define PCAL6416A_OUTPUT_CONFIG 0x4F  /* Output port config [R/W] */

#define KEYBOARD_DATA 0x60
#define KEYBOARD_STATUS 0x64

#define MAX_CHARCODE_BUFFER 256
static u8 scan_code_buffer[MAX_CHARCODE_BUFFER] = {0};
static u32 scan_code_index = 0;

static size_t read(device_t* dev, void* buf, size_t len) {
  u32 ret = 0;

  u16 data = pcal_read(PCAL6416A_INPUT);

  buf[0] = data & 0xff;
  if (len > 1) {
    buf[1] = data >> 8;
  }

  kprintf("read buf ==>%s\n", (char*)buf);

  return ret;
}

void* i2c_handler(interrupt_context_t* ic) {
  kprintf("i2c_handler\n");
  u32 val = 0;

  gic_irqack(IRQ_GPIOB_S);
  return NULL;
}

int pcal_write(u8 cmd, u16 data) {
  int twi = 0;
  char buf[3];
  buf[0] = cmd;
  buf[1] = data & 0xff;
  buf[2] = data >> 8;

  i2c_msg_t msg;
  msg.buf = buf;
  msg.len = 3;
  msg.flags = I2C_WRITE;
  msg.no = 0;
  msg.addr = PCAL6416A_I2C_ADDR;  // PCAL6416A

  sunxi_i2c_start(twi);

  int ret = sunxi_i2c_write_data(twi, &msg);
  sunxi_i2c_stop(twi);
  return ret;
}

u16 pcal_read(u8 reg) {
  int twi = 0;
  char buf[2];

  buf[0] = reg;
  i2c_msg_t msg;
  msg.buf = buf;
  msg.len = 1;
  msg.flags = I2C_WRITE;
  msg.no = 0;
  msg.addr = PCAL6416A_I2C_ADDR;  // PCAL6416A

  sunxi_i2c_start(twi);

  int ret = sunxi_i2c_write_data(twi, &msg);
  kprintf("pcal read write ret=%x\n", ret);

  buf[0] = 0;
  buf[1] = 0;
  msg.buf = buf;
  msg.len = 2;
  msg.flags = I2C_READ;
  msg.no = 0;
  msg.addr = PCAL6416A_I2C_ADDR;  // PCAL6416A
  sunxi_i2c_start(twi);

  ret = sunxi_i2c_read_data(twi, &msg);
  sunxi_i2c_stop(twi);

  return *((u16*)msg.buf);
}

void pacl_init() {
  pcal_write(PCAL6416A_CONFIG, 0xffff);

  pcal_write(PCAL6416A_INPUT_LATCH, 0);

  pcal_write(PCAL6416A_EN_PULLUPDOWN, 0xffff);

  pcal_write(PCAL6416A_SEL_PULLUPDOWN, 0xffff);

  pcal_write(PCAL6416A_INT_MASK, 0x0320);

  u16 data = pcal_read(PCAL6416A_INPUT);

  kprintf("read data %x\n", data);
}

int keyboard_init(void) {
  device_t* dev = kmalloc(sizeof(device_t), DEFAULT_TYPE);
  dev->name = "keyboard";
  dev->read = read;
  dev->id = DEVICE_KEYBOARD;
  dev->type = DEVICE_TYPE_CHAR;
  dev->data = scan_code_buffer;
  kmemset(scan_code_buffer, 0, MAX_CHARCODE_BUFFER);
  device_add(dev);
  scan_code_index = 0;

  // stdin default device
  vnode_t* stdin = vfs_find(NULL, "/dev/stdin");
  if (stdin != NULL) {
    stdin->device = device_find(DEVICE_KEYBOARD);
  }

  vnode_t* keyboard = vfs_create_node("joystick", V_FILE | V_CHARDEVICE);
  vfs_mount(NULL, "/dev", keyboard);
  keyboard->device = dev;
  keyboard->op = &device_operator;

  // gpio  KEY_IRQ
  gpio_config(GPIO_B, 4, 0xe);  // 1110:PB-EINT4
  gpio_pull(GPIO_B, 4, GPIO_PULL_UP);

  exception_regist(EX_I2C, i2c_handler);

  gic_irq_enable(IRQ_GPIOB_NS);

  pacl_init();

  return 0;
}

void keyboard_exit(void) { kprintf("keyboard exit\n"); }

module_t keyboard_module = {
    .name = "keyboard", .init = keyboard_init, .exit = keyboard_exit};
