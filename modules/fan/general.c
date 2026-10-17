/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
/* /dev/fan 的通用后端：没有 PWM 硬件的平台。
 *
 * 节点仍由抽象层（fan.c）挂上，读写都能成功 —— 档位只存在抽象层的内存里，
 * 写下去等于空操作。这样用户态 fand（app/cmd/fand.c）在所有平台都能安静地
 * 跑，不会每 2 秒刷 open 失败。
 * 要接新平台的风扇，在 ya.py 的 plat_source 里加一个 <平台>.c 实现
 * fan_hw_init() / fan_hw_set_level() 即可，抽象层不用动。 */
#include "fan.h"

int fan_hw_init(void) { return -1; }

void fan_hw_set_level(u8 level) { (void)level; }
