/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef __SYSCONF_H__
#define __SYSCONF_H__

#include "kernel/kernel.h"

/* 系统配置服务（内核模块 sysconf），实现见 sysconf.c。
 *
 * 配置文件位置：SD 卡 FAT 分区上的 /conf/system.conf
 * （源码在 app/resource/conf/system.conf，随镜像一起打到卡上）。 */

#define SYSCONF_DEFAULT_PATH "/conf/system.conf"

/* 读文件并应用（幂等，可重复调用：重新读盘再下发一次）。
 * 返回 0 = 已应用；-1 = 路径为空 / 文件读不到（此时保持驱动默认值，不算错误）。 */
int sysconf_apply(const char* path);

/* 取 [section] 段里 key 的整数值：支持十进制与 0x 十六进制。
 * 文件没加载、段不存在、key 不存在都返回 def。
 * 给其它内核代码取值用，不必各自再去读一遍文件。 */
int sysconf_get_int(const char* section, const char* key, int def);

/* 配置文件是否已成功读入内存（1 = 已加载） */
int sysconf_loaded(void);

#endif
