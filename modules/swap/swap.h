/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 10/18/26
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef SWAP_MODULE_H
#define SWAP_MODULE_H

#include "kernel/kernel.h" /* u32 / vnode_t / device_t / PAGE_SIZE */

/* 页交换（swap）模块内部接口。仅本模块使用 —— 内核侧的换出/换入能力走
 * kernel/memory.h 的 page_swap_ops_t hook（模块 init 时注册），内核不 include
 * 本头文件，也不链接本模块符号（依赖方向：module → kernel）。
 *
 * 【配置】app/resource/conf/system.conf 的 [swap] 段（两选一，都没配 = no-op）：
 *   device = sda2                 → 裸分区（连续扇区）
 *   file   = /mnt/sda1/swapfile   → 分区里的文件（vread/vwrite，无需预分配）
 *   size_mb = 64                  → 文件形式的目标大小（可省，见 swap.c）
 * 不想启用 swap：不写配置段（运行期 no-op），或从根 ya.py 的 modules 列表
 * 去掉 'swap'（编译期不存在，内核 hook 保持未注册直通）。
 *
 * 【slot 编号】对上层（回收/换入）从 0 起；落盘页号 = slot + 1，
 * 因为磁盘页 0 固定是头部（magic + 版本 + 槽位数）。这样上层拿到的
 * slot=0 不会撞头部，"slot 0 无效"的语义也天然成立。
 * 【掉电语义】换出页没有第二副本，掉电即随进程一起消失，所以头部不需要
 * 任何脏位/恢复逻辑：每次挂载都把全部数据槽视为空闲。 */

/* 1 = 已挂上 backing（分区或文件），0 = 未配置/挂载失败 */
int swap_attached(void);

/* 分配/释放一个槽。swap_alloc_slot 返回 0 = 成功（*slot 为槽号），
 * -1 = 未挂载或没有空闲槽。 */
int swap_alloc_slot(u32* slot);
void swap_free_slot(u32 slot);

/* 页粒度 IO：读/写 slot 对应的整页（PAGE_SIZE 字节）。返回 0 = 成功。 */
int swap_read_page(u32 slot, void* page);
int swap_write_page(u32 slot, void* page);

/* sysconf 配置就绪时挂载 backing（swap_init 里 sysconf_on_ready 注册）。
 * 没有配置时静默返回。 */
void swap_conf_attach(void);

#endif
