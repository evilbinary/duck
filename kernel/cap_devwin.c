/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 *
 * OBJ_DEVWIN：设备寄存器窗口对象。
 * 窗口由平台在 boot 期静态登记，用户态只能按名字拿到该对象的一个能力；
 * 映射必须落在窗口范围内，且强制 PAGE_DEV（非缓存）。
 ********************************************************************/
#include "cap.h"

#include "memory.h"
#include "libs/include/kernel/string.h"

#define log_debug

#if defined(RASPI2) || defined(RASPI3) || defined(RASPI_Z_W) || defined(RASPI4)
#define FOOT_BCM_WINS 1
#define FOOT_DEVTAB_N 6
#if defined(RASPI4)
#define FOOT_IO_BASE 0xFE000000UL
#else
#define FOOT_IO_BASE 0x3F000000UL
#endif
static const foot_dev_win_t foot_devtab[] = {
    /* 名字        物理基址                 长度        中断 */
    {"gpio", FOOT_IO_BASE + 0x00200000UL, 0x00001000UL, 0},
    {"uart0", FOOT_IO_BASE + 0x00201000UL, 0x00001000UL, 0},
    {"mbox", FOOT_IO_BASE + 0x0000B880UL, 0x00001000UL, 0},
    {"dwc2", FOOT_IO_BASE + 0x00980000UL, 0x00010000UL, 0},
    {"usb-hcd", FOOT_IO_BASE + 0x00980000UL, 0x00010000UL, 0},
    /* 粗粒度整段外设：用户态 USB/输入驱动当前需要 GPIO+DWC2+SYS_TIMER 等多处
     * 小窗口，逐个授权会把驱动改成十几次调用；先给整段，命名上标明是过渡项。 */
    {"bcm-periph", FOOT_IO_BASE, 0x01000000UL, 0},
};
#elif defined(RASPI5)
/* Pi5（BCM2712）的外设窗口。V3D 7.1（VideoCore VII）与 PM 电源域都落在
 * 64GB+ 的 36 位 AXI 地址区间（>4GB），base 是 vaddr_t 所以能装下。
 *   v3d      0x1002000000 hub + core0 + SMS，HUB_IDENT0(+0x08) 读回 "VHUB"
 *   pm-grafx 0x107D200000 PM 电源域，+0x10c 的 bit6 = V3DRSTN
 * 中断：core0 = GIC SPI 249 → INTID 281（hub = SPI 250 → 282）。
 * 映射本身经 cap_devwin_map → page_map_on 进用户页表（armv8-a 运行期建表），
 * 与内核页表的 platform_map 无关。 */
#define FOOT_DEVTAB_N 2
static const foot_dev_win_t foot_devtab[] = {
    {"v3d", 0x1002000000ULL, 0x00040000UL, 281},
    {"pm-grafx", 0x107D200000ULL, 0x00001000UL, 0},
};
#else
#define FOOT_DEVTAB_N 0
#endif

/* 每个窗口一个内核对象（boot 期建好，生命周期等同内核） */
static kobject_t* g_devwin[FOOT_DEVTAB_N > 0 ? FOOT_DEVTAB_N : 1];

const foot_dev_win_t* cap_devwin_find(const char* name) {
  if (name == NULL) {
    return NULL;
  }
#if FOOT_DEVTAB_N > 0
  for (u32 i = 0; i < FOOT_DEVTAB_N; i++) {
    if (kstrcmp((char*)foot_devtab[i].name, (char*)name) == 0) {
      return &foot_devtab[i];
    }
  }
#endif
  return NULL;
}

static int devwin_match(kobject_t* o, const char* name) {
  const foot_dev_win_t* w = (const foot_dev_win_t*)o->state;
  if (w == NULL || name == NULL) {
    return 0;
  }
  return kstrcmp((char*)w->name, (char*)name) == 0;
}

/* ---------------- ops ---------------- */

static int devwin_map(kobject_t* obj, void* pgdir, vaddr_t vaddr,
                      vaddr_t offset, vaddr_t len) {
  if (obj == NULL || obj->state == NULL || pgdir == NULL) {
    return -1;
  }
  const foot_dev_win_t* win = (const foot_dev_win_t*)obj->state;
  if (len == 0 || offset >= win->size || offset + len > win->size) {
    log_warn("devwin map: [%lx,%lx) out of %s (size %lx)\n", (unsigned long)offset,
             (unsigned long)(offset + len), win->name, (unsigned long)win->size);
    return -1;
  }
  if ((vaddr & (PAGE_SIZE - 1)) != 0 || (offset & (PAGE_SIZE - 1)) != 0 ||
      (len & (PAGE_SIZE - 1)) != 0) {
    return -1;
  }
  vaddr_t pages = len / PAGE_SIZE;
  for (vaddr_t i = 0; i < pages; i++) {
    /* 强制 PAGE_DEV：设备寄存器不能可缓存，否则写寄存器会被合并/重排 */
    page_map_on((u64*)pgdir, (u64)(vaddr + i * PAGE_SIZE),
                (u64)(win->base + offset + i * PAGE_SIZE), PAGE_DEV);
  }
  log_debug("devwin map %s off=%lx len=%lx -> va %lx\n", win->name,
            (unsigned long)offset, (unsigned long)len, (unsigned long)vaddr);
  return 0;
}

static void devwin_unmap(kobject_t* obj, void* pgdir, vaddr_t vaddr,
                         vaddr_t offset, vaddr_t len) {
  (void)obj;
  (void)offset;
  if (pgdir == NULL || len == 0) {
    return;
  }
  vaddr_t pages = (len + PAGE_SIZE - 1) / PAGE_SIZE;
  for (vaddr_t i = 0; i < pages; i++) {
    page_unmap_on((u64*)pgdir, (u64)(vaddr + i * PAGE_SIZE));
  }
}

/* 目前没有 VM 可切的对象级方法；保留分派点给将来（寄存器读写档口等） */
static int devwin_invoke(kobject_t* obj, cap_t* c, void* arg) {
  (void)obj;
  (void)c;
  (void)arg;
  return -1;
}

static void devwin_destroy(kobject_t* obj) {
  /* 窗口是平台静态资源：对象消失只是"没人持有能力了"，窗口本身不回收 */
  (void)obj;
}

static const obj_ops_t devwin_ops = {
    .ty = OBJ_DEVWIN,
    .name = "devwin",
    .invoke = devwin_invoke,
    .destroy = devwin_destroy,
    .map = devwin_map,
    .unmap = devwin_unmap,
};

/* ---------------- 对外 ---------------- */

void cap_devwin_init(void) {
  if (obj_register(&devwin_ops) < 0) {
    return; /* 已经注册过 */
  }
#if FOOT_DEVTAB_N > 0
  for (u32 i = 0; i < FOOT_DEVTAB_N; i++) {
    g_devwin[i] = obj_create(OBJ_DEVWIN, (void*)&foot_devtab[i]);
    if (g_devwin[i] == NULL) {
      log_error("devwin: create object for %s failed\n", foot_devtab[i].name);
      return;
    }
    /* boot 引用：多留一份，保证窗口对象常驻（能力都撤销后也不销毁重建） */
    obj_ref(g_devwin[i]);
  }
#endif
}

u32 cap_devwin_grant(cspace_t* cs, const char* name) {
  if (cs == NULL) {
    return 0;
  }
  kobject_t* o = obj_find(OBJ_DEVWIN, devwin_match, name);
  if (o == NULL) {
    log_warn("devwin: window '%s' not found\n", name != NULL ? name : "(null)");
    return 0;
  }
  obj_ref(o);
  u32 h = cap_install(cs, o, CAP_DEVWIN_RIGHTS_DEFAULT, 0);
  if (h == 0) {
    obj_put(o);
  }
  return h;
}

int cap_devwin_unmap(cspace_t* cs, u32 h, void* pgdir) {
  cap_t* c = cspace_get(cs, h);
  if (c == NULL || c->ty != OBJ_DEVWIN) {
    return -1;
  }
  if ((c->rights & DEVWIN_MAP) == 0) {
    return -1;
  }
  const obj_ops_t* ops = obj_ops_get(OBJ_DEVWIN);
  if (ops != NULL && ops->unmap != NULL) {
    ops->unmap(c->obj, pgdir, c->map_va, 0, c->map_len);
  }
  c->map_va = 0;
  c->map_len = 0;
  return 0;
}

int cap_devwin_info(cspace_t* cs, u32 h, u32* pa, u32* size, u32* rights) {
  cap_t* c = cspace_get(cs, h);
  if (c == NULL) {
    return -1;
  }
  if (c->ty == OBJ_DEVWIN && c->obj != NULL && c->obj->state != NULL) {
    const foot_dev_win_t* win = (const foot_dev_win_t*)c->obj->state;
    if (pa != NULL) *pa = (u32)win->base;
    if (size != NULL) *size = (u32)win->size;
  } else {
    if (pa != NULL) *pa = 0;
    if (size != NULL) *size = 0;
  }
  if (rights != NULL) *rights = c->rights;
  return 0;
}

int cap_devwin_map(cspace_t* cs, u32 h, void* pgdir, vaddr_t vaddr,
                   vaddr_t offset, vaddr_t len) {
  cap_t* c = cspace_get(cs, h);
  if (c == NULL || c->ty != OBJ_DEVWIN) {
    return -1;
  }
  if ((c->rights & DEVWIN_MAP) == 0) {
    return -1;
  }
  const obj_ops_t* ops = obj_ops_get(OBJ_DEVWIN);
  if (ops == NULL || ops->map == NULL) {
    return -1;
  }
  int r = ops->map(c->obj, pgdir, vaddr, offset, len);
  if (r == 0) {
    /* 台账：撤销时据此 unmap（多次映射只记最后一次，其余由 page_destroy 兜底） */
    c->map_va = vaddr;
    c->map_len = len;
  }
  return r;
}
