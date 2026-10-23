/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "gpu.h"
#include "dev/devfs.h"
#include "vga/vga.h"
/* 【统一配置】显示分辨率从 /conf/system.conf 读（sysconf 在 FAT 挂载后回调）。
 * 放在通用层（而不是各平台驱动里）：配置解析/优先级/自动识别对所有显示驱动
 * 一致，驱动只需实现"申请显存"和"自动识别"两个机制。 */
#ifdef SYSCONF_MODULE
#include "sysconf/sysconf.h"
#endif

size_t gpu_read(device_t* dev, void* buf, size_t len) {
  u32 ret = 0;
  return ret;
}

size_t gpu_write(device_t* dev, const void* buf, size_t len) {
  u32 ret = 0;
  vga_device_t* vga = dev->data;
  if (vga == NULL) {
    kprintf("not found vga\n");
    return ret;
  }
  kstrncpy(vga->frambuffer, (const char*)buf, len);
  return len;
}

/* ===================== Linux fbdev 标准 ioctl 兼容 =====================
 * 自己的图形栈用 VGA_IOC_*（自定义编号，见 vga/vga.h），而外来用户态程序
 * （fbcon/fbdev 后端、fbtest、SDL/Qt 的 fb 后端……）走的是 linux/fb.h 的
 * 标准编号和结构体。这里把标准编号翻译成同一套 vga 语义，使 /dev/fb0 既能
 * 被自家 libgui 用（VGA_IOC_*），也能被标准 fbdev 程序直接打开。
 * 【结构体按 aarch64 LP64 布局手写】内核侧不 include 用户态头文件；字段顺序
 * 与 linux/fb.h 严格一致，中间填充由编译器按 4/8 字节对齐自然产生。 */
#define GPU_FBIOGET_VSCREENINFO 0x4600u
#define GPU_FBIOPUT_VSCREENINFO 0x4601u
#define GPU_FBIOGET_FSCREENINFO 0x4602u
#define GPU_FBIOPAN_DISPLAY 0x4606u
#define GPU_FBIOBLANK 0x4611u

#define GPU_FB_TYPE_PACKED_PIXELS 0u
#define GPU_FB_VISUAL_TRUECOLOR 2u

typedef struct {
  u32 offset;
  u32 length;
  u32 msb_right;
} gpu_fb_bitfield_t;

/* sizeof() = 160 */
typedef struct {
  u32 xres;
  u32 yres;
  u32 xres_virtual;
  u32 yres_virtual;
  u32 xoffset;
  u32 yoffset;
  u32 bits_per_pixel;
  u32 grayscale;
  gpu_fb_bitfield_t red;
  gpu_fb_bitfield_t green;
  gpu_fb_bitfield_t blue;
  gpu_fb_bitfield_t transp;
  u32 nonstd;
  u32 activate;
  u32 height;
  u32 width;
  u32 accel_flags;
  u32 pixclock;
  u32 left_margin;
  u32 right_margin;
  u32 upper_margin;
  u32 lower_margin;
  u32 hsync_len;
  u32 vsync_len;
  u32 sync;
  u32 vmode;
  u32 rotate;
  u32 colorspace;
  u32 reserved[4];
} gpu_fb_varinfo_t;

/* sizeof() = 80（id 后 smem_start 8 字节对齐，line_length 前有 2 字节填充） */
typedef struct {
  char id[16];
  u64 smem_start;
  u32 smem_len;
  u32 type;
  u32 type_aux;
  u32 visual;
  u16 xpanstep;
  u16 ypanstep;
  u16 ywrapstep;
  u32 line_length;
  u64 mmio_start;
  u32 mmio_len;
  u32 accel;
  u16 capabilities;
  u16 reserved[2];
} gpu_fb_fixinfo_t;

/* 前向声明：重新把当前几何落到硬件（实现在 gpu_ioctl 之后） */
static int gpu_mode_reapply(vga_device_t* vga, int index);

/* 帧缓冲份数（双缓冲平台 > 1，未申报按 1） */
static u32 gpu_fb_buffers(vga_device_t* vga) {
  return vga->framebuffer_count > 0 ? vga->framebuffer_count : 1;
}

/* vga 几何/格式 → fb_var_screeninfo（分辨率、虚拟分辨率、像素位域） */
static void gpu_fb_fill_var(vga_device_t* vga, gpu_fb_varinfo_t* var) {
  u32 fmt = vga_fmt_from_bpp(vga->format, vga->bpp);
  u32 n = gpu_fb_buffers(vga);
  u32 idx = vga->framebuffer_index < n ? vga->framebuffer_index : 0;

  kmemset(var, 0, sizeof(*var));
  var->xres = vga->width;
  var->yres = vga->height;
  var->xres_virtual = vga->width;
  var->yres_virtual = vga->height * n; /* 双缓冲：虚拟高度 = 份数 × 高度 */
  var->xoffset = 0;
  var->yoffset = vga->height * idx;    /* 当前显示的是第 idx 份 */
  var->bits_per_pixel = vga->bpp;
  var->grayscale = 0;
  switch (fmt) {
    case VGA_FMT_RGB565:
      var->red.offset = 11;   var->red.length = 5;
      var->green.offset = 5;  var->green.length = 6;
      var->blue.offset = 0;   var->blue.length = 5;
      break;
    case VGA_FMT_BGR565:
      var->blue.offset = 11;  var->blue.length = 5;
      var->green.offset = 5;  var->green.length = 6;
      var->red.offset = 0;    var->red.length = 5;
      break;
    case VGA_FMT_RGB555:
      var->red.offset = 10;   var->red.length = 5;
      var->green.offset = 5;  var->green.length = 5;
      var->blue.offset = 0;   var->blue.length = 5;
      break;
    case VGA_FMT_RGB888:
      var->red.offset = 16;   var->red.length = 8;
      var->green.offset = 8;  var->green.length = 8;
      var->blue.offset = 0;   var->blue.length = 8;
      break;
    default: /* ARGB8888（32bpp）以及未申报格式 */
      var->transp.offset = 24; var->transp.length = 8;
      var->red.offset = 16;    var->red.length = 8;
      var->green.offset = 8;   var->green.length = 8;
      var->blue.offset = 0;    var->blue.length = 8;
      break;
  }
  var->activate = 0;
  var->height = vga->height; /* 物理尺寸(mm)驱动未申报，按像素数占位 */
  var->width = vga->width;
  var->rotate = (vga->rotate & VGA_ROT_MASK) * 90; /* 0/90/180/270 */
}

/* vga 几何 → fb_fix_screeninfo（显存物理地址、行长、显存大小） */
static void gpu_fb_fill_fix(vga_device_t* vga, gpu_fb_fixinfo_t* fix) {
  u32 line = vga->width * (vga->bpp / 8);

  kmemset(fix, 0, sizeof(*fix));
  kstrncpy(fix->id, "yiyiya-fb", sizeof(fix->id) - 1);
  /* 内核把显存恒等映射，frambuffer 的虚拟地址即物理地址 */
  fix->smem_start = (u64)(uintptr_t)vga->frambuffer;
  fix->line_length = line;
  /* 取"几何推算"与"驱动申报字节数"的较大者：固件分配的显存可能比估算略大
   * （对齐填充），而双缓冲平台的 framebuffer_length 可能是单份字节数。
   * 用户态按 smem_len 做 mmap，报小了会被 X 侧判"显存不足"。 */
  {
    u32 by_geom = line * vga->height * gpu_fb_buffers(vga);
    fix->smem_len =
        (vga->framebuffer_length > by_geom) ? vga->framebuffer_length : by_geom;
  }
  fix->type = GPU_FB_TYPE_PACKED_PIXELS;
  fix->visual = GPU_FB_VISUAL_TRUECOLOR;
  fix->xpanstep = 1;
  fix->ypanstep = 1;
  fix->mmio_start = 0;
  fix->accel = 0;
}

size_t gpu_ioctl(device_t* dev, u32 cmd, void* args) {
  u32 ret = 0;
  vga_device_t* vga = dev->data;
  if (vga == NULL) {
    kprintf("not found vga\n");
    return ret;
  }
  if (cmd == VGA_IOC_READ_FRAMBUFFER) {
    ret = (u32)(uintptr_t)vga->frambuffer;
  } else if (cmd == VGA_IOC_READ_FRAMBUFFER_WIDTH) {
    ret = vga->width;
  } else if (cmd == VGA_IOC_READ_FRAMBUFFER_HEIGHT) {
    ret = vga->height;
  } else if (cmd == VGA_IOC_READ_FRAMBUFFER_BPP) {
    ret = vga->bpp;
  } else if (cmd == VGA_IOC_READ_FRAMBUFFER_FORMAT) {
    /* 显示格式（VGA_FMT_*，涵盖颜色空间 + 每像素排布；0=未申报） */
    ret = vga->format;
  } else if (cmd == VGA_IOC_READ_FRAMBUFFER_TRANSFORM) {
    /* 显示变换（旋转 0/90/180/270 + 水平/垂直镜像，见 VGA_ROT_* / VGA_FLIP_*） */
    ret = vga->rotate;
  } else if (cmd == VGA_IOC_FLUSH_FRAMBUFFER) {
    if (vga->frambuffer != NULL && vga->flip_buffer != NULL) {
      u32 offset = (u32)(uintptr_t)args;
      vga->flip_buffer(vga, offset % vga->framebuffer_count);
    }
  } else if (cmd == VGA_IOC_READ_FRAMBUFFER_INFO) {
    /* 用户态是 framebuffer_info_t，比 vga_device_t 短；只拷前段安全字段 */
    if (args != NULL) {
      u32 info[12];
      info[0] = vga->width;
      info[1] = vga->height;
      info[2] = vga->bpp;
      info[3] = vga->mode;
      info[4] = (u32)(uintptr_t)vga->frambuffer;
      info[5] = vga->framebuffer_count;
      info[6] = vga->framebuffer_index;
      info[7] = vga->framebuffer_length;
      info[8] = vga->inited;
      info[9] = 0;
      info[10] = 0;
      info[11] = vga->format;
      kmemcpy(args, info, sizeof(info));
    }
    ret = (u32)(uintptr_t)vga->frambuffer;
  } else if (cmd == GPU_FBIOGET_VSCREENINFO) {
    gpu_fb_varinfo_t var;
    if (args == NULL) {
      return 0;
    }
    gpu_fb_fill_var(vga, &var);
    kmemcpy(args, &var, sizeof(var));
  } else if (cmd == GPU_FBIOGET_FSCREENINFO) {
    gpu_fb_fixinfo_t fix;
    if (args == NULL) {
      return 0;
    }
    gpu_fb_fill_fix(vga, &fix);
    kmemcpy(args, &fix, sizeof(fix));
  } else if (cmd == GPU_FBIOPUT_VSCREENINFO) {
    /* 改分辨率/色深：与当前不同就重新落到硬件；失败则回退并回写实际值，
     * 调用方按回写后的 var 判断是否被接受（fbdev 的约定）。 */
    gpu_fb_varinfo_t var;
    if (args == NULL) {
      return 0;
    }
    kmemcpy(&var, args, sizeof(var));
    if (var.xres > 0 && var.yres > 0 && var.bits_per_pixel > 0 &&
        (var.xres != vga->width || var.yres != vga->height ||
         var.bits_per_pixel != vga->bpp)) {
      u32 ow = vga->width;
      u32 oh = vga->height;
      u32 ob = vga->bpp;
      u32 om = vga->mode;
      vga->width = var.xres;
      vga->height = var.yres;
      vga->bpp = var.bits_per_pixel;
      vga->mode = 0; /* 0 = 按当前 width/height 申请，不走模式表 */
      vga->format = VGA_FMT_UNKNOWN;
      if (gpu_mode_reapply(vga, dev != NULL ? dev->index : 0) < 0) {
        log_error("gpu: PUT_VSCREENINFO %ux%u@%u rejected\n", var.xres,
                  var.yres, var.bits_per_pixel);
        vga->width = ow;
        vga->height = oh;
        vga->bpp = ob;
        vga->mode = om;
      }
    }
    gpu_fb_fill_var(vga, &var);
    kmemcpy(args, &var, sizeof(var));
  } else if (cmd == GPU_FBIOPAN_DISPLAY) {
    /* 双缓冲翻页：yoffset 落在第几份就切到第几份 */
    gpu_fb_varinfo_t var;
    u32 n = gpu_fb_buffers(vga);
    u32 idx;
    if (args == NULL) {
      return 0;
    }
    kmemcpy(&var, args, sizeof(var));
    idx = (vga->height > 0) ? (var.yoffset / vga->height) : 0;
    if (idx >= n) {
      idx = idx % n;
    }
    if (vga->frambuffer != NULL && vga->flip_buffer != NULL) {
      vga->flip_buffer(vga, idx);
    }
    vga->framebuffer_index = idx;
  } else if (cmd == GPU_FBIOBLANK) {
    /* 背光/消隐：不改变显存内容，接受即可（0=亮 4=省电） */
    ret = 0;
  }
  return ret;
}

/* 【显示实例数量】默认 1（历史行为，节点名 /dev/fb 保持兼容）。
 * 有双屏的平台（raspi5 有两路 HDMI）把 GPU_DISPLAY_COUNT 定义为 2，
 * 此时注册 /dev/fb0 与 /dev/fb1 两个节点。 */
#ifndef GPU_DISPLAY_COUNT
#define GPU_DISPLAY_COUNT 1
#endif

#if GPU_DISPLAY_COUNT > 1
/* 【多屏平台】驱动必须提供"按索引初始化"入口（如 bcm2712.c 的
 * gpu_init_mode_index）。单屏平台不需要这个符号，用 #if 隔离，
 * 避免静态库里 weak 符号解析的平台差异。 */
extern int gpu_init_mode_index(vga_device_t* vga, int mode, int index);

static int gpu_init_mode_at(vga_device_t* vga, int mode, int index) {
  return gpu_init_mode_index(vga, mode, index);
}
#else
/* 单屏平台：只有 index 0，直接走传统的 gpu_init_mode。 */
static int gpu_init_mode_at(vga_device_t* vga, int mode, int index) {
  if (index != 0) {
    return -1;
  }
  return gpu_init_mode(vga, mode);
}
#endif

/* 【显示驱动的可选用机制】不是每个平台都需要：
 *   gpu_mode_apply()      ：按 vga 当前的 width/height/bpp 重新落到硬件
 *                           （Pi5 走 mailbox 需要重新 ALLOCATE_BUFFER）。
 *   gpu_mode_auto_detect()：问硬件/固件要显示器实际分辨率（Pi5 用 GET_PHYS_WH）。
 * 平台没实现时用 weak 兜底：apply 退化为"重新走一遍 gpu_init_mode"，
 * detect 直接失败（就不做自动识别）。 */

extern int gpu_mode_apply(vga_device_t* vga, int index)
    __attribute__((weak));
extern int gpu_mode_auto_detect(vga_device_t* vga, int index)
    __attribute__((weak));

/* 重新把 vga 的当前几何落到硬件。若驱动没实现 gpu_mode_apply，
 * 退回用当前 mode 再 init 一次（多数平台 init 就是"设寄存器"，可重入）。 */
static int gpu_mode_reapply(vga_device_t* vga, int index) {
  if (gpu_mode_apply != NULL) {
    return gpu_mode_apply(vga, index);
  }
  return gpu_init_mode_at(vga, (int)vga->mode, index);
}

/* 【所有显示实例】供配置回调遍历（sysconf 就绪后统一改分辨率/重新申请）。 */
static vga_device_t* g_displays[GPU_DISPLAY_COUNT];
static int g_display_n;

/* 为一个显示实例建 device（挂 /dev/fbN）并初始化。
 * 【node_name 与 dev_name 分开】
 *   · node_name：文件系统节点名（单屏 "fb"，多屏 "fb0"/"fb1"）。
 *   · dev_name ：device_t 的 name，供 device_find_name()（即 /conf 的 [dev]
 *                绑定表、xwin/libgui 按名找设备）使用。多块同类屏必须唯一：
 *                "vga"、"vga1"…（devfs_bind_path 用 device_find_name 找设备，
 *                同名只会命中第一个）。 */
static device_t* gpu_create_display(int index, const char* node_name,
                                    const char* dev_name) {
  device_t* dev = kmalloc(sizeof(device_t), DEFAULT_TYPE);
  if (dev == NULL) return NULL;

  vga_device_t* vga = kmalloc(sizeof(vga_device_t), DEFAULT_TYPE);
  if (vga == NULL) {
    kfree(dev);
    return NULL;
  }
  /* 【必须清零】vga_device_t 里 framebuffer_count/format/rotate/brightness
   * 等字段若为垃圾值，gpu_ioctl 会按错误偏移返回、xwin 会映射错地址。 */
  kmemset(vga, 0, sizeof(vga_device_t));
  vga->frambuffer = 0;
  vga->brightness = 100;

  dev->name = (char*)dev_name;
  dev->read = gpu_read;
  dev->write = gpu_write;
  dev->ioctl = gpu_ioctl;
  dev->id = DEVICE_VGA;      /* 保持 DEVICE_VGA，xwin 靠它找显示设备 */
  dev->type = DEVICE_TYPE_VGA;
  dev->data = vga;
  dev->index = index;        /* 同一 device id 下区分第几块屏 */
  device_add(dev);

  /* 【默认显示模式】先按历史的 1024x768x32 初始化 —— 这是各平台驱动都实现的
   * 档位（如 bcm2836.c 的模式表里没有 1920x1080，硬用它会让 raspi3 等 init
   * 失败、frambuffer 留 0、xwin 报 "lcd va=0" 起不来）。
   * 【能自动识别的驱动】（如 Pi5 的 bcm2712，提供 gpu_mode_auto_detect）：
   * 初始化前先问一次真实分辨率，避免先按默认申请一块、配置回调又申请一块
   * （重复申请会丢掉一块显存）。自动识别失败就沿用默认。
   * 【注意】识别到非标准分辨率时把 mode 置 0，让驱动"按当前 width/height
   * 申请"，不要再用 mode 去查表把几何覆盖回默认档。 */
  {
    vga_device_t* tmp = vga;
    tmp->mode = VGA_MODE_1024x768x32;
    if (gpu_mode_auto_detect != NULL) {
      if (gpu_mode_auto_detect(tmp, index) == 0 &&
          tmp->width > 0 && tmp->height > 0) {
        tmp->mode = 0; /* 0 = 用当前 width/height，不走模式表 */
      }
    }
  }
  if (gpu_init_mode_at(vga, (int)vga->mode, index) < 0) {
    log_error("gpu: display %d init failed\n", index);
    /* 不返回 NULL：device 已登记，至少让 ioctl 返回 0 而不是崩。 */
  }

  /* 挂设备节点。 */
  vnode_t* node = vfs_create_node((char*)node_name, V_FILE);
  if (node != NULL) {
    vfs_mount(NULL, "/dev", node);
    node->device = dev;
    node->op = &device_operator;
  } else {
    log_error("gpu: create node %s failed\n", node_name);
  }

  /* 记下供配置回调遍历。 */
  if (g_display_n < GPU_DISPLAY_COUNT) {
    g_displays[g_display_n++] = vga;
  }
  return dev;
}

#ifdef SYSCONF_MODULE
/* 【统一应用显示配置】由 sysconf 模块在文件系统就绪后回调（登记见 gpu_init）。
 *
 * 【为什么要回调、不在这里直接读】gpu 的 init 排在 sdhci/fatfs 之前，那时 "/"
 * 还没挂，读不到 /conf/system.conf。sysconf 注册在 fatfs 之后、xwin 之前，
 * 回调时用户态还没起来、没人拿过 /dev/fb，重新申请显存是安全的。
 *
 * 【优先级】
 *   1) [hdmi] 段（或别名 [vga]）显式配置了 width/height/bpp/mode ⇒ 配置优先
 *   2) 没配 ⇒ 调驱动的自动识别（gpu_mode_auto_detect，如 Pi5 问固件/EDID）
 *   3) 自动识别也失败 ⇒ 保持驱动内置默认
 *
 * 【持久化】把"最终生效的分辨率"写回 vga，用户态从 /dev/fb 的 ioctl 读到的
 * 就是这个值，所以整条链路自动一致。
 *
 * 键（段名 [hdmi]，为兼容也接受 [vga]）：
 *   mode   = VGA_MODE_* 数值（优先，整组覆盖 width/height/bpp）
 *   width / height / bpp
 * 可只写部分键；只要最终宽高/色深与当前不同，就重新落到硬件。 */
static void gpu_apply_conf(void) {
  int v;
  u32 w = 0, h = 0, bpp = 0;
  int configured = 0;

  /* 段名优先 [hdmi]，回退 [vga]（不同平台可能用不同段名）。 */
  const char* sec = "hdmi";
  if (sysconf_get_int("hdmi", "width", -1) < 0 &&
      sysconf_get_int("hdmi", "height", -1) < 0 &&
      sysconf_get_int("hdmi", "mode", -1) < 0) {
    if (sysconf_get_int("vga", "width", -1) >= 0 ||
        sysconf_get_int("vga", "height", -1) >= 0 ||
        sysconf_get_int("vga", "mode", -1) >= 0) {
      sec = "vga";
    }
  }

  v = sysconf_get_int(sec, "mode", -1);
  if (v > 0) {
    /* mode：只认标准档；解析交给驱动不方便，这里按常见档位映射。
     * 驱动若支持 gpu_mode_apply，会按 vga->width/height 重新申请。 */
    switch (v) {
      case VGA_MODE_640x480x32:  w = 640;  h = 480;  bpp = 32; break;
      case VGA_MODE_640x480x24:  w = 640;  h = 480;  bpp = 24; break;
      case VGA_MODE_480x272x32:  w = 480;  h = 272;  bpp = 32; break;
      case VGA_MODE_1024x768x32: w = 1024; h = 768;  bpp = 32; break;
      case VGA_MODE_1280x720x32: w = 1280; h = 720;  bpp = 32; break;
      case VGA_MODE_1280x800x32: w = 1280; h = 800;  bpp = 32; break;
      case VGA_MODE_1920x1080x32:w = 1920; h = 1080; bpp = 32; break;
      case VGA_MODE_1024x600x32: w = 1024; h = 600;  bpp = 32; break;
      default: break;
    }
    if (w != 0) configured = 1;
  } else {
    v = sysconf_get_int(sec, "width", -1);
    if (v > 0) { w = (u32)v; configured = 1; }
    v = sysconf_get_int(sec, "height", -1);
    if (v > 0) { h = (u32)v; configured = 1; }
    v = sysconf_get_int(sec, "bpp", -1);
    if (v > 0) { bpp = (u32)v; configured = 1; }
  }

  log_info("gpu: [%s] %s (w=%u h=%u bpp=%u)\n", sec,
           configured ? "config" : "no config", w, h, bpp);

  for (int i = 0; i < g_display_n; i++) {
    vga_device_t* vga = g_displays[i];
    if (vga == NULL) {
      continue;
    }

    if (configured) {
      /* 配置优先：改宽高/色深（只改配置里写了的项），然后重新落地。 */
      if (w)   vga->width = w;
      if (h)   vga->height = h;
      if (bpp) vga->bpp = bpp;
      vga->format = VGA_FMT_UNKNOWN;
    } else {
      /* 没配 ⇒ 自动识别（驱动自己问硬件/固件）。失败就保持默认。 */
      if (gpu_mode_auto_detect != NULL) {
        if (gpu_mode_auto_detect(vga, i) != 0) {
          log_info("gpu: display %d auto-detect unavailable, keep %ux%u\n", i,
                   vga->width, vga->height);
          continue;
        }
      } else {
        continue; /* 驱动不支持自动识别，保持默认 */
      }
    }

    log_info("gpu: display %d -> %ux%u@%u\n", i, vga->width, vga->height,
             vga->bpp);
    if (gpu_mode_reapply(vga, i) < 0) {
      log_error("gpu: display %d reapply failed\n", i);
    }
  }
}
#endif /* SYSCONF_MODULE */

int gpu_init(void) {
  log_info("gpu_init_device end\n");

#if GPU_DISPLAY_COUNT > 1
  /* 多屏：节点 /dev/fb0、/dev/fb1 …；设备名 "vga"、"vga1" …
   * 【名字必须是静态存储期】dev->name 会一直被 device_find_name 读取
   * （vfs_create_node 另外拷一份给 vnode），用栈数组在 gpu_init 返回后
   * 就成了野指针。 */
  static char* const nodes[] = {"fb0", "fb1", "fb2", "fb3"};
  static char* const devs[] = {"vga", "vga1", "vga2", "vga3"};
  int n = (int)(sizeof(nodes) / sizeof(nodes[0]));
  for (int i = 0; i < GPU_DISPLAY_COUNT; i++) {
    const char* nd = (i < n) ? nodes[i] : "fb";
    const char* dv = (i < n) ? devs[i] : "vga";
    gpu_create_display(i, nd, dv);
  }
#else
  gpu_create_display(0, "fb", "vga");
#endif

#ifdef SYSCONF_MODULE
  /* 【登记配置回调】分辨率由 /conf/system.conf 决定（或自动识别），统一在
   * 通用层处理。sysconf（fatfs 之后、xwin 之前）会回调 gpu_apply_conf。 */
  sysconf_on_ready(gpu_apply_conf);
#endif

  return 0;
}

void gpu_exit(void) { log_info("gpu exit\n"); }

module_t gpu_module = {.name = "vga", .init = gpu_init, .exit = gpu_exit};