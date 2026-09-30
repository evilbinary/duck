/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef VGA_H
#define VGA_H

#include "kernel/kernel.h"
#include "pci/pci.h"
#define DOUBLE_BUFFER 1

enum VGA_MODE {
  VGA_MODE_80x25 = 1,
  VGA_MODE_80x160x16,
  VGA_MODE_128x128x16,
  VGA_MODE_320x200x256 ,
  VGA_MODE_480x272x24,
  VGA_MODE_480x272x32,
  VGA_MODE_480x272x16,
  VGA_MODE_480x272x18,
  VGA_MODE_640x480x24,
  VGA_MODE_640x480x32,
  VGA_MODE_1024x768x32
};

#define VGA_IOC_MAGIC 'v'
#define VGA_IOC_INIT _IO(VGA_IOC_MAGIC, 0)
#define VGA_IOC_READ_REG _IOW(VGA_IOC_MAGIC, 1, int)
#define VGA_IOC_WRITE_REG _IOW(VGA_IOC_MAGIC, 2, int)
#define VGA_IOC_READ_FRAMBUFFER _IOW(VGA_IOC_MAGIC, 3, int)
#define VGA_IOC_READ_FRAMBUFFER_WIDTH _IOW(VGA_IOC_MAGIC, 4, int)
#define VGA_IOC_READ_FRAMBUFFER_HEIGHT _IOW(VGA_IOC_MAGIC, 5, int)
#define VGA_IOC_READ_FRAMBUFFER_BPP _IOW(VGA_IOC_MAGIC, 6, int)
#define VGA_IOC_FLUSH_FRAMBUFFER _IOW(VGA_IOC_MAGIC, 7, int)
#define VGA_IOC_READ_FRAMBUFFER_INFO _IOW(VGA_IOC_MAGIC, 8, int)
/* 【显示格式/方向：由 LCD 驱动申报，应用/库按申报处理】
 * 以前只有 bpp ⇒ 16bpp 到底是 RGB555 还是 RGB565 根本区分不了 ⇒ 各应用各自猜
 * （miyoo 上就猜成了 555，屏幕是 565 ⇒ 颜色错）。现在由驱动明确申报：
 *   format：VGA_FMT_*（一个字段涵盖颜色空间与每像素排布）；UNKNOWN(0) = 未申报，
 *           由 vga_fmt_from_bpp() 按 bpp 推断（历史约定：16bpp 按 RGB555）
 *   rotate：显示变换，用"旋转角度 + 镜像"表达（VGA_ROT_* / VGA_FLIP_*，0 = 不变换） */
#define VGA_IOC_READ_FRAMBUFFER_FORMAT _IOW(VGA_IOC_MAGIC, 9, int)
#define VGA_IOC_READ_FRAMBUFFER_TRANSFORM _IOW(VGA_IOC_MAGIC, 10, int)
/* YiYiYa: 背光/亮度（0..100）—— 驱动可选实现（vga->set_brightness），
 * 未实现时 set 只记录值、get 返回记录值 ✓（console-os 设置页对接）。 */
#define VGA_IOC_SET_BRIGHTNESS _IOW(VGA_IOC_MAGIC, 11, int)
#define VGA_IOC_GET_BRIGHTNESS _IOW(VGA_IOC_MAGIC, 12, int)

/* 【显示格式：只有一个字段 `format`，值域涵盖"颜色空间 + 每像素排布"】
 * 以前 format 只有 FB_RGB/FB_NV12/FB_BGR 三个粗值（回答"要不要转 YUV、是不是 BGR"），
 * 但 16bpp 到底是 RGB555 还是 RGB565 表达不了 ⇒ 应用只能猜。现在把值域扩全，
 * 一个字段回答所有问题；gpu/gpu.h 里的 FB_* 作为【兼容别名】指向本枚举。
 *   UNKNOWN：驱动未申报 ⇒ 由 bpp 推断（历史行为：16bpp 按 RGB555） */
#define VGA_FMT_UNKNOWN 0
#define VGA_FMT_RGB555 1  /* 16bpp */
#define VGA_FMT_RGB565 2  /* 16bpp */
#define VGA_FMT_BGR565 3  /* 16bpp，R/B 交换 */
#define VGA_FMT_ARGB8888 4 /* 32bpp */
#define VGA_FMT_RGB888 5  /* 24bpp */
#define VGA_FMT_NV12 6    /* YUV420SP，需要软件转换 */

/* 未申报格式时按 bpp 推断，保持老行为 */
static inline u32 vga_fmt_from_bpp(u32 format, u32 bpp) {
  if (format != VGA_FMT_UNKNOWN) {
    return format;
  }
  if (bpp == 16) {
    return VGA_FMT_RGB555; /* 历史约定 */
  }
  if (bpp == 32) {
    return VGA_FMT_ARGB8888;
  }
  if (bpp == 24 || bpp == 18) {
    return VGA_FMT_RGB888;
  }
  return VGA_FMT_UNKNOWN;
}

/* 【显示变换：由驱动申报，应用/库据此处理】
 * 用"旋转角度 + 镜像"表达，而不是一个"是否倒装"的布尔（不同面板有 90/180/270、
 * 也有只做镜像的）。低 2 位 = 旋转，bit2/bit3 = 水平/垂直镜像；0 = 不做变换。
 * 未申报的驱动保持 0 ⇒ 行为与以前完全一致。 */
#define VGA_ROT_0 0
#define VGA_ROT_90 1
#define VGA_ROT_180 2
#define VGA_ROT_270 3
#define VGA_ROT_MASK 0x3u
#define VGA_FLIP_H (1u << 2) /* 左右镜像 */
#define VGA_FLIP_V (1u << 3) /* 上下镜像 */
/* 【面板参数】不再走 ioctl：/conf/system.conf 的 [lcd] 段由驱动自己读并应用
 * （见 duck/modules/gpu/v3s.c 的 v3s_lcd_apply_conf / duck/modules/sysconf）。 */

typedef struct vga_device vga_device_t;

typedef void (*write_pixel_fn)(vga_device_t* vga, void* buf,u32 len);
typedef void (*flip_buffer_fn)(vga_device_t* vga,u32 index);


typedef struct vga_device {
  u32 width;
  u32 height;
  u32 bpp;
  u32 mode;
  u32* frambuffer;
  u32 framebuffer_count;
  u32 framebuffer_index;
  u32 framebuffer_length;
  u32 inited;
  write_pixel_fn write;
  flip_buffer_fn flip_buffer;
  u32 format; /* 显示格式（唯一权威字段）：VGA_FMT_*，涵盖颜色空间 + 每像素排布 */
  u32* priv;
  u32* pframbuffer;
  /* 【新字段必须追加在末尾】这个构建系统不跟踪头文件依赖：若插在中间，其他模块
   * （xwin 等）仍按旧偏移读 priv/pframbuffer ⇒ 会把 pframbuffer 读成垃圾值
   * （实测 pa=2 ⇒ `xwin: fb map failed`）。追加在末尾则老目标文件完全不受影响。 */
  u32 rotate; /* VGA_ROT_* | VGA_FLIP_H | VGA_FLIP_V：驱动申报的显示变换（0=不变换） */
  /* 【新字段追加末尾】背光/亮度 0..100（默认 100）。驱动可挂 set_brightness
   * 实现真实调光（miyoo：GPIO4 开关背光）；未挂时只记录值 ✓。 */
  int brightness;
  void (*set_brightness)(vga_device_t* vga, int value);
} vga_device_t;

size_t vga_read(device_t* dev, void* buf, size_t len);
size_t vga_write(device_t* dev, const void* buf, size_t len);
size_t vga_ioctl(device_t* dev, u32 cmd, ...);
void vga_init_mode(vga_device_t* vga, int mode);

#endif