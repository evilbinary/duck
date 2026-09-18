/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef __SYSCONF_H__
#define __SYSCONF_H__

#include "kernel/kernel.h"

/* 【内核配置服务】只做三件事，不认识任何具体驱动/设备：
 *   1) 把 /conf/system.conf 读进内存并解析（段 / 键 / 值）；
 *   2) 对外提供取值接口 sysconf_get_int() / sysconf_get_str()；
 *   3) 文件系统就绪后，按注册顺序回调各驱动登记的"配置就绪"函数
 *      —— 参数怎么落到硬件，由驱动自己决定。
 * 实现与设计说明见 sysconf.c；配置样例见 app/resource/conf/system.conf。 */

#define SYSCONF_DEFAULT_PATH "/conf/system.conf"

/* 驱动登记的"配置就绪"回调（回调里直接按自己的段/键取值并写自己的寄存器）。
 *
 * 【为什么要有回调，而不是驱动 init 里直接读】
 * 驱动的 init 跑在 sdhci/fatfs 之前（见 app/init/module.c 的注册顺序），那时
 * SD 卡还没挂，驱动自己读不到 /conf/*。所以驱动在 init 里只登记一个回调
 * （登记不需要读文件，时机无所谓），等 sysconf 模块（注册在 fatfs 之后）
 * 跑到时统一触发。 */
typedef void (*sysconf_apply_fn)(void);
void sysconf_on_ready(sysconf_apply_fn fn);

/* 读文件；读完后触发所有已登记的回调。返回 0 = 读到了，-1 = 文件不可用
 * （文件不可用时也回调一次，驱动可以据此走"没有配置就用内置默认"的路）。
 * 一般不用直接调：sysconf 模块的 init 会用默认路径调它。 */
int sysconf_apply(const char* path);

/* 取 [section] 里 key 的字符串值（已去注释、去首尾空白）。
 * 返回 0 = 取到；-1 = 没取到（此时 out 置空）。 */
int sysconf_get_str(const char* section, const char* key, char* out,
                    u32 out_size);

/* 取 [section] 里 key 的整数值：支持十进制与 0x 十六进制，支持负号。
 * key 不存在 / 文件没加载 / 值不是数字 ⇒ 返回 def。
 * 【惯用法】def 传"当前值"⇒ 没配就不动；要区分"配了 0"就用 -1 之类的哨兵。 */
int sysconf_get_int(const char* section, const char* key, int def);

/* 配置文件是否已成功读入内存（1 = 已加载） */
int sysconf_loaded(void);

#endif
