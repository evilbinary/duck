/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef CPU_H
#define CPU_H

#include "kernel/kernel.h"

/* CPU 温度模块的后端接口（分层与 modules/gpu、modules/lcd 一致）：
 *
 *   cpu.c        抽象层：注册 /dev/cpu、定义 read 语义（4 字节毫摄氏度）
 *   raspi5.c     后端：firmware mailbox GET_TEMPERATURE（BCM2712/VideoCore）
 *   general.c    兜底：没有温度源的平台，永远失败（节点照样存在）
 *
 * 后端只需回答"温度从哪来"，节点注册、错误出口、打印都在抽象层。 */

/* 读 CPU 温度。成功返回 0 并把毫摄氏度写进 *milli_celsius；
 * 失败返回非 0（抽象层据此给用户态返回 0 字节，调用方自己决定怎么办）。 */
int cpu_temp_read(int* milli_celsius);

#endif
