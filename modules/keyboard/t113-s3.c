/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "gpio.h"
#include "gpio/sunxi-gpio.h"
#include "dev/devfs.h"
#include "kernel/kernel.h"
#include "keyboard.h"
#include "i2c/i2c.h"

#define KEYBOARD_DATA 0x60
#define KEYBOARD_STATUS 0x64

#define MAX_CHARCODE_BUFFER 256
static u8 scan_code_buffer[MAX_CHARCODE_BUFFER] = {0};
static u32 scan_code_index = 0;


static size_t read(device_t* dev, void* buf, size_t len) {
  u32 ret = 0;
  i2c_msg_t msg;
  msg.buf=buf;
  msg.len=len;
  msg.flags=I2C_READ;
  msg.no=0;
  msg.addr = 0x02; //PCAL6416A

  int twi=0;
  sunxi_i2c_start(twi);
  sunxi_i2c_read_data(twi,&msg);
  sunxi_i2c_stop(twi);

  // kprintf("read buf ==>%s\n",(char* )buf);

  return ret;
}

void* i2c_handler(interrupt_context_t* ic) {
  kprintf("i2c_handler\n");
  u32 val = 0;



  gic_irqack(IRQ_GPIOB_S);
  return NULL;
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

  return 0;
}

void keyboard_exit(void) { kprintf("keyboard exit\n"); }

module_t keyboard_module = {
    .name = "keyboard", .init = keyboard_init, .exit = keyboard_exit};
