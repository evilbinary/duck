/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 *
 * BCM2712 (Raspberry Pi 5) 显示驱动。
 *
 * 【硬件事实（DTB bcm2712d0-rpi-5-b.dtb 实测）】
 *   两个 HDMI 都在 BCM2712 SoC 内（不在 RP1）：
 *     /soc@107c000000/hdmi@7c701400   compatible="brcm,bcm2712-hdmi0"
 *     /soc@107c000000/hdmi@7c706400   compatible="brcm,bcm2712-hdmi1"
 *   各自有独立的 PixelValve：
 *     /soc@107c000000/pixelvalve@7c410000  "brcm,bcm2712-pixelvalve0"
 *     /soc@107c000000/pixelvalve@7c411000  "brcm,bcm2712-pixelvalve1"
 *   上游共享 MOP（显示器输出管线）：/soc@107c000000/mop@7c500000
 *
 * 【本驱动的路线 —— 固件代管（firmware mailbox）】
 *   不直接编程 HDMI PHY / PixelValve / MOP。而是通过固件 mailbox 属性通道
 *   申请一块 framebuffer，让 VideoCore 固件负责 HDMI 时序、EDID、像素时钟。
 *   Pi5 的 mailbox 与 Pi2/3 是同一个 IP（"brcm,bcm2835-mbox"），只是基址从
 *   0x7E00B880 挪到了 0x107c013880（见 bcm2835.h 的 RASPI5 分支）。
 *
 * 【双 HDMI 怎么给两块屏】每块 HDMI 各自向固件 ALLOCATE_BUFFER 申请自己的
 *   显存、各自的 SET_VIRT_OFFSET=0，画面天然独立。
 *   （曾用"共享一块 2× 高的 fb + offset"方案，实测 Pi5 固件把虚拟高当成显示
 *   几何的一部分，屏幕只出上 2/3，已弃用。）
 *
 * 【分辨率从哪来】由 /conf/system.conf 的 [hdmi] 段配置（sysconf 模块在
 *   FAT 挂载后回调本驱动的 apply 函数）。键：width / height / bpp / mode。
 *   没配就用内置默认（1920x1080x32）。为什么不在 init 里直接读文件：gpu 的
 *   init 排在 sdhci/fatfs 之前，那时 "/" 还没挂。见 sysconf.h 的说明。
 *
 * 【接口契约】和 bcm2836.c / 其它平台完全一致：
 *   实现 gpu_init_mode(vga_device_t*, int mode)，由通用 gpu.c 调用。
 *   但本驱动支持【两个显示实例】，额外导出 gpu_init_mode_index(vga, mode, idx)
 *   供 gpu.c 循环调用；gpu_init_mode 默认等价于 idx=0。
 ********************************************************************/
#include "bcm2836.h"

#include "kernel/memory.h"
#include "kernel/page.h"
#include "vga/vga.h"
/* 注：分辨率配置/自动识别的"策略"在 gpu.c（统一处理，见 gpu_apply_conf）；
 * 本文件只负责"按 vga 当前宽高向固件申请显存"的机制。 */

/* 【HDMI 数量】Pi5 有两路。索引 0 = hdmi0(7c701400)，1 = hdmi1(7c706400)。 */
#define BCM2712_HDMI_COUNT 2

/* HDMI / PixelValve 的 CPU 侧基址（父节点 /soc@107c000000 + 节点内偏移）。
 * 目前仅用于日志/后续原生驱动；本驱动走 mailbox，不直接写这些寄存器。 */
#define BCM2712_HDMI0_BASE 0x107c701400ULL
#define BCM2712_HDMI1_BASE 0x107c706400ULL
#define BCM2712_PV0_BASE 0x107c410000ULL
#define BCM2712_PV1_BASE 0x107c411000ULL
#define BCM2712_MOP_BASE 0x107c500000ULL

#define RGB24_2_RGB565(r, g, b) \
  (u16)((((r) << 8) & 0xF800) | (((g) << 3) & 0x7E0) | (((b) >> 3)))

/* 每个显示实例的状态。每块 HDMI 有自己独立的显存（不共享）。 */
typedef struct {
  vga_device_t* vga;   /* 对应的通用显示描述符（gpu.c 分配、登记在 device 上） */
  int index;           /* 0 = HDMI0，1 = HDMI1 */
  u32 fb_phys;         /* 本屏显存的物理地址（由固件分配） */
  u32 fb_bytes;        /* 本屏显存字节数 */
  u32 fb_pitch;        /* 本屏每行字节数（固件返回，含对齐） */
} bcm2712_display_t;

static bcm2712_display_t displays[BCM2712_HDMI_COUNT];
static int g_mailbox_ready;

static void write_pixel(vga_device_t* vga, uint32_t x, uint32_t y,
                        const pixel_t* pix) {
  u32 color = RGB24_2_RGB565(pix->red, pix->green, pix->blue);
  vga->frambuffer[x + y * vga->width] = color;
}

/* 把一个 u32 物理地址发给 mailbox 属性通道。
 * 【Pi5 的 buffer 地址是 CPU 物理地址】VideoCore 只能访问 32 位总线地址，
 * 所以 mailbuffer 必须落在 4GB 以内。kmalloc 出来的内存若在高地址，
 * mailbox 会读到垃圾 —— 这里用 kpage_v2p 取物理地址，过不了 4GB 就直接
 * 报错（bcm2712 早期不引入 DMA 池，先用现有分配器，必要时再改）。 */
static u32 virt_to_mbox_phys(void* ptr) {
  void* pa = kpage_v2p(ptr, 0);
  if (pa == NULL) {
    /* 已在物理直映射区：虚拟地址即物理地址 */
    pa = ptr;
  }
  return (u32)(uintptr_t)pa;
}

/* 发一条属性 tag 链，返回 mailbuffer[1] 的固件响应码。
 * 约定：mailbuffer[0] = 整个 buffer 字节数（含 8 字节头），见调用方填充。 */
static u32 mailbox_call(u32* mailbuffer) {
  u32 phys = virt_to_mbox_phys(mailbuffer);
  u32 len = mailbuffer[0];
  if (len < 16) len = 16;   /* 防御：至少把 header 刷出去 */
  /* 【cache 一致性】mailbuffer 是普通内存，固件是另一个 master，发之前必须
   * 把数据刷出到 DRAM，收之后必须让 cache 失效，否则固件读到的是旧内容。 */
  cpu_flush_dcache_range((unsigned long)mailbuffer,
                         (unsigned long)mailbuffer + len);
  mailbox_write_read(BCM2835_MAILBOX_PROP_CHANNEL, phys);
  cpu_flush_dcache_range((unsigned long)mailbuffer,
                         (unsigned long)mailbuffer + len);
  return mailbuffer[1];
}

/* 【为一块屏申请独立显存】对固件发一次 SET_PHYS_WH / SET_VIRT_WH / DEPTH /
 * PIXEL_ORDER / ALLOCATE_BUFFER。
 *
 * 【为什么每屏各申请一块，而不是"共享一块大 fb + SET_VIRT_OFFSET"】
 * 实测（Pi5 真机）：把 SET_VIRT_WH 设成 2× 物理高来做"上下分屏"会让固件的
 * 显示几何错乱 —— 屏幕只显示上 2/3 区域（固件把虚拟高当成了显示几何的一部分，
 * 而不是纯粹的滚动缓冲）。所以放弃共享方案：
 *   · SET_VIRT_WH 恒等于物理高（不做双缓冲滚动，避免几何错乱）；
 *   · 每块 HDMI 各自 ALLOCATE_BUFFER 拿自己的显存，画面天然独立。
 * 代价是固件要分配两块显存（每块 ~3MB @1024x768x32，可接受）。 */
/* 【自动识别显示模式】问固件"当前生效的物理宽高/色深"。
 * 固件本身会读 HDMI 的 EDID、按显示器选一个模式 —— 所以 GET_PHYS_WH 返回的
 * 就是"显示器实际支持的分辨率"，这就是自动识别。
 *
 * 【为什么在 ALLOCATE_BUFFER 之前问】此时固件已按 EDID 配好显示模式；直接按
 * 它的值申请 fb，显示几何就与显示器一致，不会出现居中留边/像素错位。
 * 返回 0 = 拿到有效值（写入 *w/*h/*bpp），非 0 = 固件没给（调用方用请求值）。 */
static int bcm2712_query_native(u32* w, u32* h, u32* bpp) {
  u32* mb = kmalloc_alignment(sizeof(u32) * 24, 16, DEVICE_TYPE);
  if (mb == NULL) return -1;

  u32 i = 0;
  mb[i++] = 0;  /* 长度占位 */
  mb[i++] = 0;  /* 响应码占位 */
  mb[i++] = BCM2835_VC_TAG_GET_PHYS_WH; mb[i++] = 8; mb[i++] = 0;
  mb[i++] = 0; mb[i++] = 0;
  mb[i++] = BCM2835_VC_TAG_GET_DEPTH;   mb[i++] = 4; mb[i++] = 0; mb[i++] = 0;
  mb[i++] = 0;  /* 结束 */
  mb[0] = i * 4;

  u32 resp = mailbox_call(mb);
  u32 rw = mb[5], rh = mb[6], rb = mb[10];
  kfree_alignment(mb);

  log_info("bcm2712: query native resp=%x -> %dx%d depth=%d\n", resp, rw, rh,
           rb);
  if (resp != BCM2835_MAILBOX_SUCCESS || rw < 16 || rh < 16) {
    return -1;
  }
  *w = rw;
  *h = rh;
  if (rb == 16 || rb == 24 || rb == 32) {
    *bpp = rb;
  }
  return 0;
}

static int bcm2712_alloc_fb_one(vga_device_t* vga, int index) {
  u32* mb = kmalloc_alignment(sizeof(u32) * 32, 16, DEVICE_TYPE);
  if (mb == NULL) {
    log_error("bcm2712: no mem for mailbuffer\n");
    return -1;
  }

  /* 分辨率已由 gpu.c 的统一配置逻辑（[hdmi] 配置 / 自动识别）定好，
   * 这里只负责"按 vga 当前的 width/height/bpp 向固件申请显存"。 */
  u32 i = 0;
  mb[i++] = 0;      /* 占位：稍后回填总长度（字节） */
  mb[i++] = 0;      /* 占位：响应码 */

  mb[i++] = BCM2835_VC_TAG_SET_PHYS_WH;
  mb[i++] = 8;
  mb[i++] = 8;
  mb[i++] = vga->width;
  mb[i++] = vga->height;

  /* 虚拟高 == 物理高（单缓冲）。不能用 2× 做分屏，见上。 */
  mb[i++] = BCM2835_VC_TAG_SET_VIRT_WH;
  mb[i++] = 8;
  mb[i++] = 8;
  mb[i++] = vga->width;
  mb[i++] = vga->height;

  mb[i++] = BCM2835_VC_TAG_SET_DEPTH;
  mb[i++] = 4;
  mb[i++] = 4;
  mb[i++] = vga->bpp;

  mb[i++] = BCM2835_VC_TAG_SET_PIXEL_ORDER;
  mb[i++] = 4;
  mb[i++] = 4;
  mb[i++] = 0;

  mb[i++] = BCM2835_VC_TAG_ALLOCATE_BUFFER;
  mb[i++] = 8;
  mb[i++] = 8;
  mb[i++] = 16;                   /* 请求对齐（字节） */
  mb[i++] = 0;                    /* 返回：fb 物理地址 */

  mb[i++] = 0;                    /* 链结束标记（tag 0，长度 0） */

  /* 【buffer 长度】按官方属性通道约定：mb[0] = 整个 buffer 的字节数，
   * 含开头 8 字节（长度 + 响应码）。 */
  mb[0] = i * 4;

  u32 resp = mailbox_call(mb);
  if (resp != BCM2835_MAILBOX_SUCCESS) {
    log_error("bcm2712: hdmi%d ALLOCATE_BUFFER failed resp=%x\n", index, resp);
    kfree_alignment(mb);
    return -1;
  }

  /* 【诊断】把固件对每个 tag 的响应打出来 —— 固件可能改写请求的分辨率/
   * 色深（例如按 EDID 强制成别的模式），不看清就无法解释"只显示 2/3"。 */
  log_info("bcm2712: hdmi%d resp phys_wh=%dx%d virt_wh=%dx%d depth=%d "
           "pixel_order=%d align/fb=%d size=%d\n",
           index, mb[5], mb[6], mb[10], mb[11], mb[15], mb[19], mb[23], mb[24]);

  /* fb 物理地址：Pi5 不做 0x3FFFFFFF 掩码（那是 Pi2/3 的 GPU 总线别名）。
   * ALLOCATE_BUFFER 的响应：buffer[0](mb[23]) = fb 物理地址（覆盖请求的对齐
   * 值），buffer[1](mb[24]) = 字节数。这是 rpi 固件的约定。 */
  u32 fb_phys = mb[23] + GPU_MEM_BASE;
  u32 fb_size = mb[24];
  u32 pitch = vga->width * (vga->bpp / 8);

  /* 询问固件实际 pitch（可能因对齐 > width*bytes_per_px）。 */
  {
    u32* pm = kmalloc_alignment(sizeof(u32) * 8, 16, DEVICE_TYPE);
    if (pm != NULL) {
      pm[0] = 7 * 4;
      pm[1] = 0;
      pm[2] = BCM2835_VC_TAG_GET_PITCH;
      pm[3] = 4;
      pm[4] = 4;
      pm[5] = 0;
      pm[6] = 0;
      u32 r2 = mailbox_call(pm);
      if (r2 == BCM2835_MAILBOX_SUCCESS && pm[5] != 0) {
        pitch = pm[5];
      }
      kfree_alignment(pm);
    }
  }

  /* 【固件实际给的是多少 bpp？—— 用 GET_PITCH 反推，并【据此申报格式】】
   *
   * 实测（Pi5 真机）：请求 SET_DEPTH=32，但固件 GET_PITCH 回 1920×2=3840、
   * ALLOCATE_BUFFER 的 size 也是 1920×1080×2。两个独立来源一致指向 **16bpp**；
   * GET_DEPTH 回 32 只是固件回显请求值，不代表实际分配格式。
   *
   * 【分工】驱动只负责"如实申报" fb 的真实几何 + 格式；把 32bpp 画布转成
   * 硬件色深这件事由【用户态 libgui/screen】完成（那里已有 rgb2nv12 的先例，
   * 见 eggs/libgui/screen.c 的 screen_flush）。
   *
   * 【格式取值】16bpp 申报 VGA_FMT_RGB565（值 2）：
   *   · 不能用 VGA_FMT_RGB555（值 1）—— libgui/SDL 里 `format==1` 是老的
   *     "NV12" 约定（screen.c 会据此做 rgb2nv12），数值撞车会误触发转换。
   *   · 固件默认像素序是 RGB565（SET_PIXEL_ORDER=0）。 */
  if (vga->width > 0 && pitch >= vga->width) {
    u32 real_bpp = (u32)((u64)pitch / vga->width) * 8;
    if (real_bpp == 16 || real_bpp == 24 || real_bpp == 32) {
      if (real_bpp != vga->bpp) {
        log_info("bcm2712: hdmi%d fix bpp %u -> %u (pitch=%u w=%u size=%u)\n",
                 index, vga->bpp, real_bpp, pitch, vga->width, fb_size);
      }
      vga->bpp = real_bpp;
    }
  }
  /* 申报格式（用户态据此决定画布与转换方式）。 */
  if (vga->bpp == 16) {
    vga->format = VGA_FMT_RGB565;
  } else if (vga->bpp == 24) {
    vga->format = VGA_FMT_RGB888;
  } else {
    vga->format = VGA_FMT_ARGB8888;
  }
  log_info("bcm2712: hdmi%d format=%u (bpp=%u pitch=%u)\n", index, vga->format,
           vga->bpp, pitch);

  /* 【让这一路 HDMI 显示本块显存】SET_VIRT_OFFSET(0,0)。
   * 每屏显示自己的 fb、offset 归零 —— 这是"两屏独立"的关键：不能省。 */
  {
    u32* om = kmalloc_alignment(sizeof(u32) * 8, 16, DEVICE_TYPE);
    if (om != NULL) {
      om[0] = 8 * 4;
      om[1] = 0;
      om[2] = BCM2835_VC_TAG_SET_VIRT_OFFSET;
      om[3] = 8;
      om[4] = 8;
      om[5] = 0;
      om[6] = 0;
      om[7] = 0;
      mailbox_call(om);
      kfree_alignment(om);
    }
  }

  displays[index].fb_phys = fb_phys & ~0x3Fu;
  displays[index].fb_bytes = fb_size;
  displays[index].fb_pitch = pitch;

  /* 【回读诊断】再问固件"现在实际生效的几何"——GET_PHYS_WH / GET_VIRT_WH /
   * GET_DEPTH / GET_PIXEL_ORDER。SET_* 的响应码只代表"语法接受"，不代表固件
   * 真按这个值配了显示；只有 GET 回来的才是权威。画面几何不对时看这组值。 */
  {
    u32* gm = kmalloc_alignment(sizeof(u32) * 32, 16, DEVICE_TYPE);
    if (gm != NULL) {
      u32 k = 0;
      gm[k++] = 0;  /* 长度占位 */
      gm[k++] = 0;  /* 响应码占位 */
      /* GET_PHYS_WH */
      gm[k++] = BCM2835_VC_TAG_GET_PHYS_WH; gm[k++] = 8; gm[k++] = 0;
      gm[k++] = 0; gm[k++] = 0;
      /* GET_VIRT_WH */
      gm[k++] = BCM2835_VC_TAG_GET_VIRT_WH; gm[k++] = 8; gm[k++] = 0;
      gm[k++] = 0; gm[k++] = 0;
      /* GET_DEPTH */
      gm[k++] = BCM2835_VC_TAG_GET_DEPTH; gm[k++] = 4; gm[k++] = 0; gm[k++] = 0;
      /* GET_PIXEL_ORDER */
      gm[k++] = BCM2835_VC_TAG_GET_PIXEL_ORDER; gm[k++] = 4; gm[k++] = 0;
      gm[k++] = 0;
      /* GET_PITCH */
      gm[k++] = BCM2835_VC_TAG_GET_PITCH; gm[k++] = 4; gm[k++] = 0; gm[k++] = 0;
      gm[k++] = 0;  /* 结束 */
      gm[0] = k * 4;
      u32 gr = mailbox_call(gm);
      log_info("bcm2712: hdmi%d GET resp=%x phys=%dx%d virt=%dx%d depth=%d "
               "order=%d pitch=%d\n",
               index, gr, gm[5], gm[6], gm[10], gm[11], gm[15], gm[19], gm[23]);
      kfree_alignment(gm);
    }
  }

  log_info("bcm2712: hdmi%d fb phys=%x size=%x pitch=%d %dx%d\n", index,
           displays[index].fb_phys, fb_size, pitch, vga->width, vga->height);

  kfree_alignment(mb);
  return 0;
}

/* 翻页/提交：把该屏的当前内容生效。
 * 本驱动是单缓冲（frambuffer 直接就是固件 fb 对应段），所以 flush 只需
 * 把 cache 刷到 DRAM，让 VideoCore 的 DMA 看到最新像素。 */
static void bcm2712_flip_screen(vga_device_t* vga, u32 index) {
  (void)index;
  if (vga == NULL || vga->frambuffer == NULL) return;
  u32 bytes = vga->width * vga->height * (vga->bpp / 8);
  cpu_flush_dcache_range((unsigned long)vga->frambuffer,
                         (unsigned long)vga->frambuffer + bytes);
}

/* 【模式 → 宽/高/色深】。未知模式回退 1920x1080x32（HDMI 最通用）。 */
static void bcm2712_mode_to_wh(int mode, u32* w, u32* h, u32* bpp) {
  switch (mode) {
    case VGA_MODE_640x480x32:  *w = 640;  *h = 480;  *bpp = 32; break;
    case VGA_MODE_640x480x24:  *w = 640;  *h = 480;  *bpp = 24; break;
    case VGA_MODE_480x272x32:  *w = 480;  *h = 272;  *bpp = 32; break;
    case VGA_MODE_1024x768x32: *w = 1024; *h = 768;  *bpp = 32; break;
    case VGA_MODE_1280x720x32: *w = 1280; *h = 720;  *bpp = 32; break;
    case VGA_MODE_1280x800x32: *w = 1280; *h = 800;  *bpp = 32; break;
    case VGA_MODE_1920x1080x32:*w = 1920; *h = 1080; *bpp = 32; break;
    case VGA_MODE_1024x600x32: *w = 1024; *h = 600;  *bpp = 32; break;
    case VGA_MODE_80x25:       *w = 80;   *h = 25;   *bpp = 8;  break;
    default:                   *w = 1920; *h = 1080; *bpp = 32; break;
  }
}

/* 【申请一块独立显存 + 映射 + 回填 vga 描述符】按 index 对应的 HDMI。
 * 幂等可重入：改分辨率后会再调一次（旧显存由固件回收，这里不 kfree）。
 * 对外导出为 gpu_mode_apply()：gpu.c 在应用 [hdmi]/[vga] 配置时调用，
 * 用当前 vga->width/height/bpp 重新申请显存。 */
int bcm2712_setup_display(vga_device_t* vga, int index) {
  if (vga == NULL || index < 0 || index >= BCM2712_HDMI_COUNT) {
    return -1;
  }

  /* 每屏各自向固件申请：见 bcm2712_alloc_fb_one 的说明。 */
  if (bcm2712_alloc_fb_one(vga, index) < 0) {
    return -1;
  }

  /* 【把本屏显存映射进内核页表 + 记入 VMA 模板】
   * vaddr 用物理地址本身（恒等映射），这样 frambuffer 指针即物理地址，
   * 用户态首次访问时按 PAGE_FB 属性镜像到自己的页表。 */
  vmemory_map_phys((vaddr_t)displays[index].fb_phys,
                   (vaddr_t)displays[index].fb_phys,
                   (vaddr_t)displays[index].fb_bytes, MEMORY_FB, PAGE_FB);

  vga->pframbuffer = (u32*)(uintptr_t)displays[index].fb_phys;
  vga->frambuffer = (u32*)(uintptr_t)displays[index].fb_phys;
  vga->framebuffer_length = displays[index].fb_bytes;
  vga->framebuffer_count = 1;   /* 单缓冲：不靠 offset 翻页 */
  vga->framebuffer_index = 0;
  vga->inited = 1;
  return 0;
}

/* 【对外入口】把 vga 里当前的 width/height/bpp 真正落到硬件（重新申请显存）。
 * gpu.c 的统一配置逻辑（gpu_apply_conf）在算好目标分辨率后调这个。
 * 非 Pi5 平台没有"重新申请"的概念，返回 -1 表示不支持，gpu.c 会忽略。 */
int gpu_mode_apply(vga_device_t* vga, int index) {
  return bcm2712_setup_display(vga, index);
}

/* 【平台自带的分辨率自动识别】问固件当前显示模式（固件已按 EDID 选好）。
 * 返回 0 = 拿到有效值并写入 vga；非 0 = 没拿到（gpu.c 就用手上的值）。
 * 供 gpu.c 的统一配置逻辑在"用户没配 [hdmi]"时调用。 */
int gpu_mode_auto_detect(vga_device_t* vga, int index) {
  u32 nw, nh, nb;
  if (vga == NULL) return -1;
  nw = vga->width;
  nh = vga->height;
  nb = vga->bpp;
  if (bcm2712_query_native(&nw, &nh, &nb) != 0) {
    return -1;
  }
  if (nw != vga->width || nh != vga->height) {
    log_info("bcm2712: hdmi%d auto-detect %ux%u (was %ux%u)\n", index, nw, nh,
             vga->width, vga->height);
  }
  vga->width = nw;
  vga->height = nh;
  vga->bpp = nb;
  return 0;
}

/* 【按索引初始化】gpu.c 对每个 HDMI 调一次。 */
int gpu_init_mode_index(vga_device_t* vga, int mode, int index) {
  if (vga == NULL || index < 0 || index >= BCM2712_HDMI_COUNT) {
    return -1;
  }

  /* 分辨率/色深。 */
  bcm2712_mode_to_wh(mode, &vga->width, &vga->height, &vga->bpp);
  vga->mode = mode;
  vga->format = VGA_FMT_UNKNOWN;   /* 由 bpp 推断 */
  vga->rotate = VGA_ROT_0;
  vga->write = NULL;
  vga->flip_buffer = bcm2712_flip_screen;

  /* 【mailbox 寄存器映射】属性通道寄存器所在的页按 Device 映射。
   * bcm2836.c 也是在这里 page_map；幂等，多实例重复调用无害。 */
  page_map(BCM2835_MAILBOX_BASE & ~0xfffULL,
           BCM2835_MAILBOX_BASE & ~0xfffULL, PAGE_DEV);

  displays[index].vga = vga;
  displays[index].index = index;

  /* 【每屏各自向固件申请一块独立显存 + 映射 + 填 vga 描述符】 */
  if (bcm2712_setup_display(vga, index) < 0) {
    return -1;
  }

  g_mailbox_ready = 1;
  return 0;
}

/* 【兼容入口】单实例默认走 HDMI0，行为与旧平台驱动一致。 */
int gpu_init_mode(vga_device_t* vga, int mode) {
  return gpu_init_mode_index(vga, mode, 0);
}

/* 【探针】只做 mailbox 连通性检查，不分配显存。用于 bring-up 阶段确认
 * Pi5 固件是否应答（返回 0 表示通）。可由 platform 早期调用。 */
int bcm2712_mailbox_probe(void) {
  u32* mb = kmalloc_alignment(sizeof(u32) * 8, 16, DEVICE_TYPE);
  if (mb == NULL) return -1;

  page_map(BCM2835_MAILBOX_BASE & ~0xfffULL,
           BCM2835_MAILBOX_BASE & ~0xfffULL, PAGE_DEV);

  /* GET_FIRMWARE_REVISION(0x00000001)：最无害的只读 tag。 */
  mb[0] = 7 * 4;   /* 头 2 槽 + tag 4 槽 + 结束标记 1 槽 */
  mb[1] = 0;
  mb[2] = 0x00000001;
  mb[3] = 4;
  mb[4] = 4;
  mb[5] = 0;
  mb[6] = 0;

  u32 resp = mailbox_call(mb);
  u32 rev = mb[5];
  kfree_alignment(mb);

  log_info("bcm2712: mailbox probe resp=%x fw_rev=%x\n", resp, rev);
  return (resp == BCM2835_MAILBOX_SUCCESS) ? 0 : -1;
}

/* 【配置由 gpu.c 统一处理】本文件不再读 /conf/system.conf。
 *   · 分辨率来自 [hdmi] 配置 → gpu.c 的 gpu_apply_conf() 解析后调
 *     gpu_mode_apply()（即 bcm2712_setup_display）重新申请；
 *   · 没配 → gpu.c 调 gpu_mode_auto_detect()（本文件导出）问固件要。 */
