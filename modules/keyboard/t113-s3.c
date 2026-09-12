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
#define PCAL6416A_INPUT1 0x01         /* Input port [RO] */
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

#define GPIO_HIGH 1
#define GPIO_LOW 0

#define DEF_GPIO_KEY(name, status) \
  { name, (name##_PIN), status }

#define KEY_UP_PIN 1<<3 //f7
#define KEY_DOWN_PIN 1<<1 //fd
#define KEY_LEFT_PIN 1<<4 //ef
#define KEY_RIGHT_PIN 1<<0 //fe
#define KEY_HOME_PIN 1<<11  //

#define KEY_BUTTON_A_PIN 1<<14 //bbff
#define KEY_BUTTON_B_PIN 1<<12 //ebff
#define KEY_BUTTON_X_PIN 1<<13 //dbff
#define KEY_BUTTON_Y_PIN 1<<15 //f3ff

#define KEY_BUTTON_SELECT_PIN 1<<7 //fb7f
#define KEY_BUTTON_START_PIN 1<<6 //fbbf
#define KEY_BUTTON_L1_PIN 1<<2  //fbfb
#define KEY_BUTTON_L2_PIN 1<<8  //faff
#define KEY_BUTTON_R1_PIN 1<<15  //7bff
#define KEY_BUTTON_R2_PIN 1<<9  //f9ff
#define KEY_POWER_PIN 1<<5      //

struct gpiopins {
  int key;
  int pin;
  int status;
} pins[] = {
    DEF_GPIO_KEY(KEY_UP, -1),
    DEF_GPIO_KEY(KEY_DOWN, -1),
    DEF_GPIO_KEY(KEY_LEFT, -1),
    DEF_GPIO_KEY(KEY_RIGHT, -1),
    DEF_GPIO_KEY(KEY_BUTTON_A, -1),
    DEF_GPIO_KEY(KEY_BUTTON_B, -1),
    DEF_GPIO_KEY(KEY_BUTTON_X, -1),
    DEF_GPIO_KEY(KEY_BUTTON_Y, -1),
    DEF_GPIO_KEY(KEY_BUTTON_SELECT, -1),
    DEF_GPIO_KEY(KEY_BUTTON_START, -1),
    DEF_GPIO_KEY(KEY_BUTTON_L1, -1),
    DEF_GPIO_KEY(KEY_BUTTON_L2, -1),
    DEF_GPIO_KEY(KEY_BUTTON_R1, -1),
    DEF_GPIO_KEY(KEY_BUTTON_R2, -1),
    DEF_GPIO_KEY(KEY_POWER, -1),
    DEF_GPIO_KEY(KEY_HOME, -1),
};

int pcal_write(u8 cmd, u16 data);
u16 pcal_read(u8 reg);

/* 板子无 PCAL6416A 时禁止继续轮询 I2C，避免刷屏 */
static int pcal_present = 0;

static size_t read(device_t* dev, void* buf, size_t len) {
  u32 ret = 0;
  (void)dev;
  (void)len;

  if (!pcal_present) {
    return 0;
  }

  u16 gpio0 = pcal_read(PCAL6416A_INPUT);
  u16 gpio1 = pcal_read(PCAL6416A_INPUT1);
  u16 data = gpio0 & 0xff | gpio1 << 8;

  /* 【诊断·可删】只在按键位图发生变化时打印原始值，用于核对 pins[] 掩码与实际
   * 硬件是否一致（若一直不打印，说明扩展器根本没看到按下 ⇒ 掩码/接线/地址问题；
   * 若打印了但应用无反应 ⇒ 问题在 read() 返回值/事件投递那一段）。 */
  {
    static u16 dbg_last_data = 0xffff;
    if (data != dbg_last_data) {
      dbg_last_data = data;
      kprintf("keypad raw=%x (down bits = %x)\n", (u32)data, (u32)(~data & 0xffff));
    }
  }

  //kprintf("data =%x\n",data);

  char* keys = (char*)buf;
  int key_cnt = 0;
  u32 scan_code = 0;

  for (int i = 0; i < 16; i++) {
    int val = data & pins[i].pin;

    // kprintf("key %d val data %x pin %x val %x\n",i,data,pins[i].pin, val);


    if (scan_code_index > MAX_CHARCODE_BUFFER) {
      scan_code_index = 0;
      log_warn("key buffer is full\n");
    }

    /* 【修复·只在状态变化时上报】原来按键按住期间每轮询一次就压入一个"按下"事件
     * ⇒ 队列被重复事件灌满（还触发 key buffer is full），应用也会一直看到按下、
     * 松手之后还在持续。改为只在"松开→按下"的边沿上报一次。 */
    if (val == 0) {
      /* 注意 pins[] 的初值是 -1（不是 0）⇒ 必须用 "!= 1" 判断"非按下→按下"，
       * 用 "== 0" 会把第一次按下（-1）判掉、此后永远不发事件（实测症状：
       * keypad raw 有变化，但应用侧 joypad press 一行都没有）。 */
      if (pins[i].status != 1) {
        pins[i].status = 1;  // down
        scan_code = pins[i].key;
        keys++;
        key_cnt++;

        scan_code_buffer[scan_code_index++] = scan_code;
      }

      // kprintf("press key down %d %d\n",i,scan_code);

    } else if (pins[i].status == 1) {
      pins[i].status = 0;  // up
      scan_code = pins[i].key | 0x80;
      scan_code_buffer[scan_code_index++] = scan_code;
      // kprintf("press key up %d %d\n",i,scan_code);
      keys++;
      key_cnt++;
    } else {
      // pins[i].status = 0;
    }
  }

  if (scan_code_index > 0) {
    /* 【修复·事件顺序】原来取的是 scan_code_buffer[scan_code_index-1]（【最新】
     * 那个）却把队首丢掉 ⇒ 消费者拿到的是乱序/重复的按下与松开 ⇒ 应用侧刚置上的
     * keyPad 位可能立刻被一个"松开"清掉 ⇒ 游戏只看到一帧的按下（够触发音效，
     * 不足以进入关卡）。改为标准的【先进先出】：取最旧的一个、整体前移。
     * 同时返回 1（本次确实只写出 1 字节），避免调用方按返回值以为有多个字节。 */
    kstrncpy(buf, &scan_code_buffer[0], 1);
    for (int i = 0; i + 1 < scan_code_index; i++) {
      scan_code_buffer[i] = scan_code_buffer[i + 1];
    }
    scan_code_index--;
    ret = 1;
  }

  // kprintf("pres key_cnt %d scan_code_index %d\n",scan_code_index,key_cnt);


  /* 返回真实写出的字节数（最多 1 字节）。原来返回 key_cnt（可能是 2、3…），
   * 调用方 event_read_joystick() 会按返回值当作有效字节数 ⇒ 语义不符。 */
  if (ret > 0) {
    /* 【诊断·可删】出队侧：应用到底拿到了哪个字节（0x80 位=松开） */
    static u32 dbg_evt;
    if (dbg_evt < 24u) {
      dbg_evt++;
      kprintf("keypad evt=%x q=%u\n", (u32)(*(u8*)buf), scan_code_index);
    }
  }
  return ret;
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

  if (!pcal_present) {
    return 0xffff;
  }

  buf[0] = reg;
  i2c_msg_t msg;
  msg.buf = buf;
  msg.len = 1;
  msg.flags = I2C_WRITE;
  msg.no = 0;
  msg.addr = PCAL6416A_I2C_ADDR;  // PCAL6416A

  sunxi_i2c_start(twi);

  int ret = sunxi_i2c_write_data(twi, &msg);
  // kprintf("pcal read write ret=%x\n", ret);
  if ((u32)ret == (u32)-1) {
    return 0xffff;
  }

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
  if ((u32)ret == (u32)-1) {
    return 0xffff;
  }

  return *((u16*)msg.buf);
}

void* i2c_handler(interrupt_context_t* ic) {
  kprintf("i2c_handler\n");
  u32 val = 0;

  gic_irqack(IRQ_GPIOB_S);
  return NULL;
}

void pacl_init() {
  int ret = pcal_write(PCAL6416A_CONFIG, 0xffff);
  if ((u32)ret == (u32)-1) {
    pcal_present = 0;
    kprintf("pcal6416a not present, skip gpio keypad\n");
    return;
  }

  pcal_present = 1;

  pcal_write(PCAL6416A_INPUT_LATCH, 0);

  pcal_write(PCAL6416A_EN_PULLUPDOWN, 0xffff);

  pcal_write(PCAL6416A_SEL_PULLUPDOWN, 0xffff);

  pcal_write(PCAL6416A_INT_MASK, 0x0320);

  u16 data = pcal_read(PCAL6416A_INPUT);

  kprintf("pcal6416a ok, input %x\n", data);
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
