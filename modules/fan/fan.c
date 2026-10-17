/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
/* 风扇模块：抽象层。
 *
 * 只做三件事：把 /dev/fan 节点挂上、持有当前档位、把档位转给后端
 * fan_hw_set_level()。PWM 怎么产生由各平台文件实现（见 fan.h 的分层说明）：
 *   raspi5.c   RP1 PWM1 通道 3 / GPIO45 / ~24kHz 反相
 *   general.c  没有 PWM 硬件（init 失败，节点照样存在）
 *
 * 【接口】/dev/fan：read/write 各 1 字节 = 档位 0-10。
 * 温控策略在用户态 app/cmd/fand.c（读 /dev/cpu -> 查表 -> 写 /dev/fan），
 * 内核只提供机制不跑策略：内核线程 + printk 在这套内核上会互踩栈。
 ********************************************************************/
#include "fan.h"
#include "kernel/memory.h"
#include "dev/devfs.h"

static u8 fan_level = FAN_LEVEL_DEFAULT;

/* /dev/fan：唯一接口，两个字节就够。
 *   read  1 字节 = 当前档位
 *   write 1 字节 = 设档位（0 = 关通道，10 = 满速） */
static size_t fan_read(device_t* dev, void* buf, size_t len) {
  (void)dev;
  if (len < 1) return 0;
  ((u8*)buf)[0] = fan_level;
  return 1;
}

static size_t fan_write(device_t* dev, const void* buf, size_t len) {
  (void)dev;
  if (len < 1) return 0;
  u8 level = ((const u8*)buf)[0];
  if (level > FAN_LEVEL_MAX) level = FAN_LEVEL_MAX;
  fan_level = level;
  fan_hw_set_level(fan_level);
  return 1;
}

int fan_init(void) {
  if (fan_hw_init() == 0) {
    /* 上电兜底档位。后端 init 里已应用过一次，这里再设一遍保证
     * "抽象层持有唯一真相"，重复写占空比是幂等的。 */
    fan_hw_set_level(fan_level);
  } else {
    kprintf("fan: no PWM hardware, /dev/fan only stores level\n");
  }

  device_t* fan_dev = kmalloc(sizeof(device_t), DEFAULT_TYPE);
  fan_dev->name = "fan";
  fan_dev->read = fan_read;
  fan_dev->write = fan_write;
  fan_dev->id = DEVICE_FAN;
  fan_dev->type = DEVICE_TYPE_CHAR;
  device_add(fan_dev);

  vnode_t* vnode = vfs_create_node("fan", V_FILE);
  vfs_mount(NULL, "/dev", vnode);
  vnode->device = device_find(DEVICE_FAN);
  vnode->op = &device_operator;

  return 0;
}

void fan_exit(void) { fan_hw_set_level(0); }

module_t fan_module = {.name = "fan", .init = fan_init, .exit = fan_exit};
