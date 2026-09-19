/*******************************************************************
* Copyright 2021-present evilbinary
* 作者: evilbinary on 01/01/20
* 邮箱: rootdebug@163.com
********************************************************************/
#ifndef GPU_H
#define GPU_H

#include "kernel/kernel.h"


/* 【兼容别名】旧的"大类"取值现在统一指向 vga/vga.h 的 VGA_FMT_*（一个字段回答
 * "颜色空间 + 每像素排布"）。新代码请直接用 VGA_FMT_*，别再引入第三套命名。 */
#define FB_RGB VGA_FMT_RGB555 /* 16bpp 的 RGB 就是 RGB555 */
#define FB_NV12 VGA_FMT_NV12
#define FB_BGR VGA_FMT_BGR565


#endif
