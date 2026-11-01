/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 *
 * OBJ_ARENA：物理【连续】大块内存对象（DMA / GPU 页表 / QPU 代码 / 渲染目标）。
 *
 * 与 OBJ_FRAME 的区别：
 *   FRAME 从页池逐页 mm_alloc_page 分配，单对象 ≤256KB，且要求逐页恰好连续
 *   （页池碎片化后大块基本拿不到）；
 *   ARENA 走 phy_alloc_aligment（mm_alloc_zero_align），一次拿到整块物理连续 +
 *   恒等映射，支持 4KB 对齐与 MB 级长度 —— V3D 的 MMU 页表必须是 4MB 物理连续，
 *   只有它给得起。
 *
 * 【生命周期】owned=1 时，对象最后一个能力消失 → kfree_alignment 归还整块；
 * 映射进用户页表后不参与 page_destroy 归还（见 arena_map 的约束），由本对象
 * 自身回收。pa 是 64 位物理地址（恒等映射下即内核虚拟地址）。
 ********************************************************************/
#include "cap.h"

#include "memory.h"
#include "libs/include/kernel/string.h"

#define log_debug

/* 单次分配上限：4MB 页表 + QPU 代码块 + uniform + TMU scratch 绰绰有余，
 * 又不至于一句 ioctl 吃光内核堆。画布（几十 MB）走 memfd，不经这里。 */
#define ARENA_MAX_SIZE (16u * 1024 * 1024)

static inline int arena_aligned(vaddr_t v) {
  return (v & (PAGE_SIZE - 1)) == 0;
}

/* ---------------- ops ---------------- */

static int arena_map(kobject_t* obj, void* pgdir, vaddr_t vaddr,
                     vaddr_t offset, vaddr_t len) {
  if (obj == NULL || obj->state == NULL || pgdir == NULL) {
    return -1;
  }
  const foot_arena_t* a = (const foot_arena_t*)obj->state;
  if (len == 0 || offset >= a->size || offset + len > a->size) {
    log_warn("arena map: [%lx,%lx) out of arena (size %lx)\n",
             (unsigned long)offset, (unsigned long)(offset + len),
             (unsigned long)a->size);
    return -1;
  }
  if (!arena_aligned(vaddr) || !arena_aligned(offset) || !arena_aligned(len)) {
    return -1;
  }
  /* 【VA 约束】ARENA 是内核堆连续块，不属于页池；一旦映射到用户区
   * （>= EXEC_ADDR），进程退出时 page_destroy 会把它当普通用户页 mm_free_page
   * 归还 —— 内核堆块被推进页池空闲链，整机崩。所以只允许映射到 EXEC_ADDR
   * 以下的 MMIO 直通区（那里 page_destroy 不扫）。 */
  if (vaddr >= (vaddr_t)EXEC_ADDR) {
    log_warn("arena map: vaddr %lx must be below EXEC_ADDR %lx\n",
             (unsigned long)vaddr, (unsigned long)EXEC_ADDR);
    return -1;
  }
  vaddr_t pages = len / PAGE_SIZE;
  for (vaddr_t i = 0; i < pages; i++) {
    vaddr_t pa = a->pa + offset + i * PAGE_SIZE;
    /* 可缓存 normal 映射：GPU 页表/QPU 代码要 CPU 快速访问 + dcache 维护。
     * 不用 page_ref：ARENA 不在页池，引用由对象自身的 refs 管理。 */
    page_map_on((u64*)pgdir, (u64)(vaddr + i * PAGE_SIZE), (u64)pa, PAGE_USER);
  }
  log_debug("arena map pa=%lx off=%lx len=%lx -> va %lx\n", (unsigned long)a->pa,
            (unsigned long)offset, (unsigned long)len, (unsigned long)vaddr);
  return 0;
}

static void arena_unmap(kobject_t* obj, void* pgdir, vaddr_t vaddr,
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

static void arena_destroy(kobject_t* obj) {
  if (obj == NULL || obj->state == NULL) {
    return;
  }
  foot_arena_t* a = (foot_arena_t*)obj->state;
  if (a->owned && a->pa != 0) {
    /* 整块归还：phy_alloc_aligment 与 kfree_alignment 配对（内核堆恒等映射） */
    kfree_alignment((void*)a->pa);
  }
  kfree(a);
  obj->state = NULL;
}

static int arena_invoke(kobject_t* obj, cap_t* c, void* arg) {
  (void)obj;
  (void)c;
  (void)arg;
  return -1; /* 目前没有对象级方法，全部走 cap_map/unmap */
}

static const obj_ops_t arena_ops = {
    .ty = OBJ_ARENA,
    .name = "arena",
    .invoke = arena_invoke,
    .destroy = arena_destroy,
    .map = arena_map,
    .unmap = arena_unmap,
};

/* ---------------- 对外 ---------------- */

void cap_arena_init(void) { obj_register(&arena_ops); }

u32 cap_arena_alloc(cspace_t* cs, vaddr_t size, vaddr_t alignment, u32 rights) {
  if (cs == NULL) {
    return 0;
  }
  if (size == 0) {
    size = PAGE_SIZE;
  }
  size = (size + PAGE_SIZE - 1) & ~(vaddr_t)(PAGE_SIZE - 1);
  if (size == 0 || size > ARENA_MAX_SIZE) {
    log_warn("arena: size %lx out of range (1..%x)\n", (unsigned long)size,
             ARENA_MAX_SIZE);
    return 0;
  }
  if (alignment < PAGE_SIZE) {
    alignment = PAGE_SIZE;
  }
  /* 只支持 2 的幂对齐（mm_alloc_zero_align 的约束） */
  if ((alignment & (alignment - 1)) != 0) {
    return 0;
  }

  thread_t* t = thread_current();

  foot_arena_t* a = kmalloc(sizeof(foot_arena_t), KERNEL_TYPE);
  if (a == NULL) {
    return 0;
  }
  kmemset(a, 0, sizeof(foot_arena_t));
  a->size = size;
  a->owner = (t != NULL) ? t->id : 0;

  /* 物理连续 + 恒等映射（内核堆块）。失败（内存不够/水位线）返回 NULL。 */
  void* p = phy_alloc_aligment((size_t)size, (int)alignment);
  if (p == NULL) {
    log_error("arena: alloc %lx bytes (align %lx) failed\n", (unsigned long)size,
              (unsigned long)alignment);
    kfree(a);
    return 0;
  }
  a->pa = (vaddr_t)p;
  a->owned = 1;

  kobject_t* o = obj_create(OBJ_ARENA, a);
  if (o == NULL) {
    kfree_alignment(p);
    kfree(a);
    return 0;
  }
  if (rights == 0) {
    rights = CAP_ARENA_RIGHTS_DEFAULT;
  }
  u32 h = cap_install(cs, o, rights, 0);
  if (h == 0) {
    obj_put(o); /* 归零 → arena_destroy 归还整块 */
    return 0;
  }
  log_debug("arena alloc pa=%lx size=%lx h=%x\n", (unsigned long)a->pa,
            (unsigned long)a->size, h);
  return h;
}

int cap_arena_map(cspace_t* cs, u32 h, void* pgdir, vaddr_t vaddr,
                  vaddr_t offset, vaddr_t len) {
  cap_t* c = cspace_get(cs, h);
  if (c == NULL || c->ty != OBJ_ARENA) {
    return -1;
  }
  if ((c->rights & ARENA_MAP) == 0) {
    return -1;
  }
  const obj_ops_t* ops = obj_ops_get(OBJ_ARENA);
  if (ops == NULL || ops->map == NULL) {
    return -1;
  }
  int r = ops->map(c->obj, pgdir, vaddr, offset, len);
  if (r == 0) {
    c->map_va = vaddr;
    c->map_len = len;
    c->map_off = offset;
  }
  return r;
}

int cap_arena_unmap(cspace_t* cs, u32 h, void* pgdir) {
  cap_t* c = cspace_get(cs, h);
  if (c == NULL || c->ty != OBJ_ARENA) {
    return -1;
  }
  if ((c->rights & ARENA_MAP) == 0) {
    return -1;
  }
  const obj_ops_t* ops = obj_ops_get(OBJ_ARENA);
  if (ops != NULL && ops->unmap != NULL) {
    ops->unmap(c->obj, pgdir, c->map_va, c->map_off, c->map_len);
  }
  c->map_va = 0;
  c->map_len = 0;
  c->map_off = 0;
  return 0;
}

int cap_arena_info(cspace_t* cs, u32 h, u32* pa, u32* size, u32* rights) {
  cap_t* c = cspace_get(cs, h);
  if (c == NULL) {
    return -1;
  }
  if (c->ty == OBJ_ARENA && c->obj != NULL && c->obj->state != NULL) {
    const foot_arena_t* a = (const foot_arena_t*)c->obj->state;
    if (pa != NULL) *pa = (u32)a->pa;
    if (size != NULL) *size = (u32)a->size;
  } else {
    if (pa != NULL) *pa = 0;
    if (size != NULL) *size = 0;
  }
  if (rights != NULL) *rights = c->rights;
  return 0;
}
