/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 *
 * OBJ_FRAME：物理页（页组）对象。
 *   对象持有页池的一部分：创建时 ref=1，映射给用户再加一份，
 *   最后一个能力消失时把对象那份还回去（ref 归零才真正 mm_free_page）。
 *
 * 【为什么要单独的对象】sys_thread_map 原先把"物理地址落在设备窗口内"
 * 当作唯一授权，普通内存页没有任何能力覆盖 ⇒ 用户态只要知道一个物理地址
 * 就能把它映射进来（等同于读遍内核/他人内存）。有了 FRAME，内存页也能
 * 像设备窗口一样被授权、转授、撤销。
 ********************************************************************/
#include "cap.h"

#include "memory.h"
#include "page_ref.h"

#define log_debug

static inline int frame_aligned(vaddr_t v) { return (v & (PAGE_SIZE - 1)) == 0; }

/* ---------------- ops ---------------- */

static int frame_map(kobject_t* obj, void* pgdir, vaddr_t vaddr,
                     vaddr_t offset, vaddr_t len) {
  if (obj == NULL || obj->state == NULL || pgdir == NULL) {
    return -1;
  }
  const foot_frame_t* f = (const foot_frame_t*)obj->state;
  if (len == 0 || offset >= f->size || offset + len > f->size) {
    log_warn("frame map: [%lx,%lx) out of frame (size %lx)\n",
             (unsigned long)offset, (unsigned long)(offset + len),
             (unsigned long)f->size);
    return -1;
  }
  if (!frame_aligned(vaddr) || !frame_aligned(offset) || !frame_aligned(len)) {
    return -1;
  }
  vaddr_t pages = len / PAGE_SIZE;
  for (vaddr_t i = 0; i < pages; i++) {
    vaddr_t pa = f->pa + offset + i * PAGE_SIZE;
    /* 映射本身再持有一份引用：用户进程退出时 page_destroy 减掉这一份，
     * 对象那份仍然留着，避免"对象还在、页已被还进页池"。 */
    page_ref_inc((u32)pa);
    page_map_on((u64*)pgdir, (u64)(vaddr + i * PAGE_SIZE), (u64)pa, PAGE_USER);
  }
  log_debug("frame map pa=%lx off=%lx len=%lx -> va %lx\n",
            (unsigned long)f->pa, (unsigned long)offset, (unsigned long)len,
            (unsigned long)vaddr);
  return 0;
}

static void frame_unmap(kobject_t* obj, void* pgdir, vaddr_t vaddr,
                        vaddr_t offset, vaddr_t len) {
  if (obj == NULL || obj->state == NULL || pgdir == NULL || len == 0) {
    return;
  }
  const foot_frame_t* f = (const foot_frame_t*)obj->state;
  vaddr_t pages = (len + PAGE_SIZE - 1) / PAGE_SIZE;
  for (vaddr_t i = 0; i < pages; i++) {
    page_unmap_on((u64*)pgdir, (u64)(vaddr + i * PAGE_SIZE));
    /* 与 frame_map 的 inc 配对；越界的那几页不认账（映射时会先拒绝） */
    if (offset + (i + 1) * PAGE_SIZE <= f->size) {
      page_ref_dec((u32)(f->pa + offset + i * PAGE_SIZE));
    }
  }
}

static void frame_destroy(kobject_t* obj) {
  if (obj == NULL || obj->state == NULL) {
    return;
  }
  foot_frame_t* f = (foot_frame_t*)obj->state;
  if (f->owned) {
    for (vaddr_t off = 0; off < f->size; off += PAGE_SIZE) {
      vaddr_t pa = f->pa + off;
      /* 归零 = 没有别处还在用，可以还给页池 */
      if (page_ref_dec((u32)pa) == 0) {
        mm_free_page((void*)pa);
      }
    }
  }
  kfree(f);
  obj->state = NULL;
}

static int frame_invoke(kobject_t* obj, cap_t* c, void* arg) {
  (void)obj;
  (void)c;
  (void)arg;
  return -1; /* 目前没有对象级方法，全部走 cap_map/unmap */
}

static const obj_ops_t frame_ops = {
    .ty = OBJ_FRAME,
    .name = "frame",
    .invoke = frame_invoke,
    .destroy = frame_destroy,
    .map = frame_map,
    .unmap = frame_unmap,
};

/* ---------------- 对外 ---------------- */

void cap_frame_init(void) { obj_register(&frame_ops); }

u32 cap_frame_alloc(cspace_t* cs, vaddr_t size, vaddr_t pa, u32 rights) {
  if (cs == NULL) {
    return 0;
  }
  if (size == 0) {
    size = PAGE_SIZE;
  }
  size = (size + PAGE_SIZE - 1) & ~(vaddr_t)(PAGE_SIZE - 1);
  if (size == 0 || size > (vaddr_t)(64 * PAGE_SIZE)) {
    return 0; /* 单次最多 256KB，防止一句 ioctl 吃光页池 */
  }
  thread_t* t = thread_current();

  foot_frame_t* f = kmalloc(sizeof(foot_frame_t), KERNEL_TYPE);
  if (f == NULL) {
    return 0;
  }
  kmemset(f, 0, sizeof(foot_frame_t));
  f->size = size;
  f->owner = (t != NULL) ? t->id : 0;

  if (pa != 0) {
    /* 包装已有物理页：只登记，不拥有（DMA / 共享页对端自己管理生命周期） */
    if (!frame_aligned(pa)) {
      kfree(f);
      return 0;
    }
    f->pa = pa;
    f->owned = 0;
  } else {
    /* 从页池逐页分配；任一页失败就回滚已分到的页 */
    for (vaddr_t off = 0; off < size; off += PAGE_SIZE) {
      void* p = mm_alloc_page();
      if (p == NULL) {
        log_error("frame: alloc failed at %lx/%lx\n", (unsigned long)off,
                  (unsigned long)size);
        for (vaddr_t r = 0; r < off; r += PAGE_SIZE) {
          mm_free_page((void*)(f->pa + r));
        }
        kfree(f);
        return 0;
      }
      if (!mm_page_in_ram(p)) {
        mm_free_page(p);
        for (vaddr_t r = 0; r < off; r += PAGE_SIZE) {
          mm_free_page((void*)(f->pa + r));
        }
        kfree(f);
        return 0;
      }
      /* 首帧的地址顺带当基址；非连续时对象语义不成立，拒绝 */
      if (off == 0) {
        f->pa = (vaddr_t)p;
      } else if ((vaddr_t)p != f->pa + off) {
        log_warn("frame: not contiguous (%lx vs %lx)\n", (unsigned long)p,
                 (unsigned long)(f->pa + off));
        mm_free_page(p);
        for (vaddr_t r = 0; r < off; r += PAGE_SIZE) {
          mm_free_page((void*)(f->pa + r));
        }
        kfree(f);
        return 0;
      }
      cpu_zero_phy_page(p, PAGE_SIZE);
      page_ref_set((u32)(vaddr_t)p, 1); /* 对象持有的那一份 */
    }
    f->owned = 1;
  }

  kobject_t* o = obj_create(OBJ_FRAME, f);
  if (o == NULL) {
    if (f->owned) {
      for (vaddr_t off = 0; off < f->size; off += PAGE_SIZE) {
        mm_free_page((void*)(f->pa + off));
      }
    }
    kfree(f);
    return 0;
  }
  /* obj_create 给的 refs=1 正好是该能力的引用，不再 obj_ref */
  if (rights == 0) {
    rights = CAP_FRAME_RIGHTS_DEFAULT;
  }
  u32 h = cap_install(cs, o, rights, 0);
  if (h == 0) {
    obj_put(o); /* 归零 → frame_destroy 归还页 */
    return 0;
  }
  log_debug("frame alloc pa=%lx size=%lx h=%x\n", (unsigned long)f->pa,
            (unsigned long)f->size, h);
  return h;
}

int cap_frame_map(cspace_t* cs, u32 h, void* pgdir, vaddr_t vaddr,
                  vaddr_t offset, vaddr_t len) {
  cap_t* c = cspace_get(cs, h);
  if (c == NULL || c->ty != OBJ_FRAME) {
    return -1;
  }
  if ((c->rights & FRAME_MAP) == 0) {
    return -1;
  }
  const obj_ops_t* ops = obj_ops_get(OBJ_FRAME);
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

int cap_frame_unmap(cspace_t* cs, u32 h, void* pgdir) {
  cap_t* c = cspace_get(cs, h);
  if (c == NULL || c->ty != OBJ_FRAME) {
    return -1;
  }
  if ((c->rights & FRAME_MAP) == 0) {
    return -1;
  }
  const obj_ops_t* ops = obj_ops_get(OBJ_FRAME);
  if (ops != NULL && ops->unmap != NULL) {
    ops->unmap(c->obj, pgdir, c->map_va, c->map_off, c->map_len);
  }
  c->map_va = 0;
  c->map_len = 0;
  c->map_off = 0;
  return 0;
}

int cap_frame_info(cspace_t* cs, u32 h, u32* pa, u32* size, u32* rights) {
  cap_t* c = cspace_get(cs, h);
  if (c == NULL) {
    return -1;
  }
  if (c->ty == OBJ_FRAME && c->obj != NULL && c->obj->state != NULL) {
    const foot_frame_t* f = (const foot_frame_t*)c->obj->state;
    if (pa != NULL) *pa = (u32)f->pa;
    if (size != NULL) *size = (u32)f->size;
  } else {
    if (pa != NULL) *pa = 0;
    if (size != NULL) *size = 0;
  }
  if (rights != NULL) *rights = c->rights;
  return 0;
}
