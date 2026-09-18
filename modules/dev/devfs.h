/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef DEVFS_H
#define DEVFS_H

#include "kernel/kernel.h"
#include "kernel/device.h"
#include "kernel/fd.h"
#include "kernel/module.h"
#include "kernel/vfs.h"

extern voperator_t no_rw_operator;
extern voperator_t device_operator;

/* ---------------- 设备节点绑定（配置驱动） ----------------
 * 【为什么集中到这三个 API】
 * /dev/fb、/dev/lcd 这类"共享节点名"以前是各显示驱动自己 vfs_create_node() +
 * 直接写 node->device，于是"后注册的覆盖先注册的"：st7789 就曾把 gpu 建好的
 * /dev/fb 改写成 128x128 的 SPI 屏，应用问到的分辨率跟着变成 128x128。
 * 现在统一走这里：
 *   · 驱动只挂自己独占的名字，如 devfs_bind_device("/dev/gpu0", dev)；
 *   · 主显示/共享别名（/dev/fb）由配置（/conf/system.conf 的 [dev] 段）或
 *     内置优先级决定，见 sysconf 的 sysconf_apply_dev()；
 *   · 本 API 是唯一允许改写节点 device 的入口，日志集中，"谁绑了谁"可追溯。
 * 设备的定位用【名字】而不是 id：多实例设备（多块屏）注册唯一 name
 * （"vga"/"vga1"/"lcd"/"lcd1"）即可，不用往 device.h 的枚举里加 id。 */

/* 把 path 节点绑定到 dev：节点不存在则创建（只接受 /dev/<name>）。
 * dev == NULL ⇒ 解绑（device 置空；device_read/ioctl 对 NULL 有保护，读会返回 0）。
 * 返回 0 成功；-1 = 路径非法/创建失败。 */
int devfs_bind_device(const char* path, device_t* dev);

/* 按【设备名】绑定（配置里写的是名字）：
 *   name 为 "none"/"-" ⇒ 解绑；
 *   name 为空/NULL ⇒ 视为"没配"，跳过（避免把写漏的值当成解绑）；
 *   设备名不存在 ⇒ 返回 -1 且不建节点（否则应用 open 成功却拿到 NULL device）。
 * 返回 0 成功；-1 失败（已打日志）。 */
int devfs_bind_path(const char* path, const char* name);

/* 主显示的默认优先级：GPU/RGB(DEVICE_VGA) > QEMU(DEVICE_VGA_QEMU) > SPI 屏
 * (DEVICE_LCD)。没有 [dev] 配置时用它决定 /dev/fb —— 于是"有 GPU 没 LCD"和
 * "有 LCD 没 GPU"的平台都能自动选对，且与模块注册顺序无关。
 * 注意与 devfs_pick_output_device() 不同：那个是 stdio 用（优先 SERIAL）。
 * 一个都没有时返回 NULL。 */
device_t* devfs_display_primary(void);

#endif