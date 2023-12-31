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

#define MAX_CHARCODE_BUFFER 32
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
  log_debug("keyboard pins init\n");

  for (int i = 0; i < 16; i++) {
    log_debug("keyboard pins config %d\n", i);
    gpio_config(0, _pins[i].pin, 1);
    log_debug("keyboard pins pull %d\n", i);
    gpio_pull(0, _pins[i].pin, !_pins[i].active);
  }
  log_debug("keyboard pins init end\n");
}

static size_t read(device_t* dev, void* buf, size_t len) {
  u32 ret = 0;

  char* keys = (char*)buf;
  int key_cnt = 0;

  for (int i = 0; i < sizeof(_pins) / sizeof(struct gpio_pins); i++) {
    int val = gpio_input(_pins[i].pin);
    // log_debug("pin %d %d\n", i, val);

    if (val == _pins[i].active) {
      *keys = _pins[i].key;
      keys++;
      key_cnt++;
      if (key_cnt >= len) break;
    }
  }

  return key_cnt > 0 ? key_cnt : -1;

  //   if (scan_code_index > 0) {
  //     kstrncpy(buf, &scan_code_buffer[scan_code_index - 1], 1);
  //     for (int i = 0; i < scan_code_index; i++) {
  //       scan_code_buffer[i] = scan_code_buffer[i + 1];
  //     }
  //     scan_code_index--;
  //     ret = 1;
  //   }
  //   return ret;
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

  vnode_t* keyboard = vfs_create_node("keyboard", V_FILE | V_CHARDEVICE);
  vfs_mount(NULL, "/dev", keyboard);
  keyboard->device = dev;
  keyboard->op = &device_operator;

  init_gpio();

  return 0;
}

void keyboard_exit(void) { kprintf("keyboard exit\n"); }

module_t keyboard_module = {
    .name = "keyboard", .init = keyboard_init, .exit = keyboard_exit};
