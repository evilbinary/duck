/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef FAN_H
#define FAN_H

#include "kernel/kernel.h"

/* 档位 0-10：0 = gate off 通道，10 = 满占空比。
 * /dev/fan 只收发 1 字节档位，"档位 -> 占空比"的换算属于后端。 */
#define FAN_LEVEL_MAX 10
#define FAN_LEVEL_DEFAULT 5

/* 风扇模块的分层（与 modules/cpu、modules/gpu、modules/lcd 一致）：
 *
 *   fan.c      抽象层：注册 /dev/fan、持有当前档位、定义 read/write 语义
 *   raspi5.c   后端：RP1 PWM1 通道 3 / GPIO45 / ~24kHz 反相
 *   general.c  兜底：没有 PWM 硬件，init 失败、写档位是空操作
 *
 * 后端只回答"怎么把档位变成波形"，节点、状态、范围检查都在抽象层。 */

/* 初始化 PWM。成功返回 0（此后 fan_hw_set_level 生效），失败返回非 0。 */
int fan_hw_init(void);

/* 设置档位 0..FAN_LEVEL_MAX。初始化没成功时后端应自行忽略。 */
void fan_hw_set_level(u8 level);

#endif
