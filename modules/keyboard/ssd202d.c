/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "gpio/ssd202d.h"

#include "dev/devfs.h"
#include "kernel/kernel.h"
#include "keyboard.h"
#include "pic/pic.h"

#define KEYBOARD_DATA 0x60
#define KEYBOARD_STATUS 0x64

#define MAX_CHARCODE_BUFFER 256
static u8 scan_code_buffer[MAX_CHARCODE_BUFFER] = {0};
static u32 scan_code_index = 0;

#define KEY_UP_PIN 1
#define KEY_DOWN_PIN 69
#define KEY_LEFT_PIN 70
#define KEY_RIGHT_PIN 5
#define KEY_HOME_PIN 12
#define KEY_BUTTON_A_PIN 7
#define KEY_BUTTON_B_PIN 6
#define KEY_BUTTON_X_PIN 9
#define KEY_BUTTON_Y_PIN 8
#define KEY_BUTTON_SELECT_PIN 11
#define KEY_BUTTON_START_PIN 10
#define KEY_BUTTON_L1_PIN 14
#define KEY_BUTTON_L2_PIN 13
#define KEY_BUTTON_R1_PIN 47
#define KEY_BUTTON_R2_PIN 90
#define KEY_POWER_PIN 86

#define DECLARE_GPIO_KEY(name, level) \
  { name, name##_PIN, level, !level }

struct gpio_pins {
  int key;
  int pin;
  int active;
  int status;
} _pins[] = {
    DECLARE_GPIO_KEY(KEY_UP, GPIO_LOW),
    DECLARE_GPIO_KEY(KEY_DOWN, GPIO_LOW),
    DECLARE_GPIO_KEY(KEY_LEFT, GPIO_LOW),
    DECLARE_GPIO_KEY(KEY_RIGHT, GPIO_LOW),
    DECLARE_GPIO_KEY(KEY_BUTTON_A, GPIO_LOW),
    DECLARE_GPIO_KEY(KEY_BUTTON_B, GPIO_LOW),
    DECLARE_GPIO_KEY(KEY_BUTTON_X, GPIO_LOW),
    DECLARE_GPIO_KEY(KEY_BUTTON_Y, GPIO_LOW),
    DECLARE_GPIO_KEY(KEY_BUTTON_SELECT, GPIO_LOW),
    DECLARE_GPIO_KEY(KEY_BUTTON_START, GPIO_LOW),
    DECLARE_GPIO_KEY(KEY_BUTTON_L1, GPIO_LOW),
    DECLARE_GPIO_KEY(KEY_BUTTON_L2, GPIO_LOW),
    DECLARE_GPIO_KEY(KEY_BUTTON_R1, GPIO_LOW),
    DECLARE_GPIO_KEY(KEY_BUTTON_R2, GPIO_LOW),
    DECLARE_GPIO_KEY(KEY_POWER, GPIO_HIGH),
    DECLARE_GPIO_KEY(KEY_HOME, GPIO_LOW),
};

static void init_gpio(void) {
  // log_debug("keyboard pins init\n");

  for (int i = 0; i < 16; i++) {
    // log_debug("keyboard pins config %d\n", i);
    gpio_config(0, _pins[i].pin, 1);
    // log_debug("keyboard pins pull %d\n", i);
    gpio_pull(0, _pins[i].pin, !_pins[i].active);
  }
  // log_debug("keyboard pins init end\n");
}

/* 【按键队列：先进先出 + 只在电平跳变时入队】
 * 原实现两处硬伤（实测：日志被 `key buffer is full` 刷爆，游戏"按一下只有 1 帧效果"
 * ⇒ 看着像卡在标题画面）：
 *   ① `if (val == active)` 分支里**每次 read 都入队一个"按下"** ⇒ 按住不放时每帧都在
 *      塞事件 ⇒ 队列瞬间满；
 *   ② 满了以后 `scan_code_index = 0`，把**【已排队的】用户按键整体丢弃**（应丢新事件）。
 * 出队侧原来取 `scan_code_buffer[index-1]`（**最新**那个）且移位方向错 ⇒ 消费者拿到
 * 乱序/重复的按下与松开 ⇒ 应用刚置上的 keyPad 位立刻被"松开"清掉 ⇒ 只看到一帧的按下。
 * 现在：边沿触发入队、满则丢新并限速打印、出队取最旧（FIFO）。 */
static void scan_code_push(u32 code) {
  if (scan_code_index >= MAX_CHARCODE_BUFFER) {
    static u32 full_dbg = 0;
    if (full_dbg < 8u) {
      full_dbg++;
      log_warn("key queue full, drop code=%x (further msgs suppressed)\n", code);
    }
    return;
  }
  scan_code_buffer[scan_code_index++] = code;
}

static size_t read(device_t* dev, void* buf, size_t len) {
  u32 ret = 0;

  for (int i = 0; i < 16; i++) {
    int val = gpio_input(0, _pins[i].pin);

    if (val == _pins[i].active) {
      /* 【按下沿】只在状态变化时入队一次（原实现每次 read 都入队 ⇒ 队列洪泛 ✗） */
      if (_pins[i].status != 1) {
        _pins[i].status = 1;
        scan_code_push(_pins[i].key);
      }

    } else if (_pins[i].status == 1) {
      /* 【松开沿】 */
      _pins[i].status = 0;
      scan_code_push(_pins[i].key | 0x80);
    }
  }

  if (scan_code_index > 0) {
    /* FIFO：取【最旧】的一个，其余整体前移（原实现取最新 + 移位方向错 ✗） */
    kstrncpy(buf, &scan_code_buffer[0], 1);
    for (int i = 0; i + 1 < scan_code_index; i++) {
      scan_code_buffer[i] = scan_code_buffer[i + 1];
    }
    scan_code_index--;
    ret = 1;
  }

  /* 返回**实际写出的字节数**（0/1）：调用方 event_read_joystick()/event_read_key()
   * 按返回值当有效字节数用；旧实现返回本轮扫到的按键个数（可能 2、3…）⇒ 语义不符。 */
  return ret;
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

  init_gpio();

  return 0;
}

void keyboard_exit(void) { kprintf("keyboard exit\n"); }

module_t keyboard_module = {
    .name = "keyboard", .init = keyboard_init, .exit = keyboard_exit};
