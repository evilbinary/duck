/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 *
 * capability v2 的 CSpace 与原语层。
 *   句柄 = (gen << 12) | slot，只在所属 CSpace 内有意义
 *   CSpace 挂 TCB：同进程线程共享靠 ref 计数，fork 时整表复制
 *   撤销用「父槽号 + 本表线性扫描」，不维护 child 链表（v1 就是那里漏清槽
 *   导致 double free：ya_free bad state size=40）
 ********************************************************************/
#include "cap.h"

#include "memory.h"
#include "thread.h"

#define log_debug

void cap_init(void) {
  obj_init();
  cap_devwin_init();   /* 静态设备窗口 → OBJ_DEVWIN 对象 */
  cap_frame_init();    /* 物理页对象 OBJ_FRAME */
  cap_arena_init();    /* 物理连续大块对象 OBJ_ARENA */
  cap_endpoint_init(); /* IPC 端点 OBJ_ENDPOINT */
}

/* ---------------- 工具 ---------------- */

static u32 cap_handle_of(cspace_t* cs, u32 slot) {
  return ((cs->gens[slot] & CAP_GEN_MASK) << CAP_SLOT_BITS) | slot;
}

/* 槽位下标 → 有效句柄（给"按槽号反查"的内部调用用） */
static u32 cap_handle_slot(cspace_t* cs, u32 slot) {
  if (slot == 0 || slot >= cs->nslot || cs->slots[slot].obj == NULL) {
    return 0;
  }
  return cap_handle_of(cs, slot);
}

/* 容量不足时翻倍（上限 CSPACE_MAX_SLOTS），失败返回 -1 */
static int cspace_grow(cspace_t* cs) {
  if (cs->nslot >= CSPACE_MAX_SLOTS) {
    return 0; /* 已到上限，装不下就由调用方报错 */
  }
  u32 n = cs->nslot * 2;
  if (n > CSPACE_MAX_SLOTS) {
    n = CSPACE_MAX_SLOTS;
  }
  cap_t* slots = kmalloc(sizeof(cap_t) * n, KERNEL_TYPE);
  u32* gens = kmalloc(sizeof(u32) * n, KERNEL_TYPE);
  if (slots == NULL || gens == NULL) {
    if (slots != NULL) kfree(slots);
    if (gens != NULL) kfree(gens);
    return -1;
  }
  kmemset(slots, 0, sizeof(cap_t) * n);
  kmemset(gens, 0, sizeof(u32) * n);
  kmemcpy(slots, cs->slots, sizeof(cap_t) * cs->nslot);
  kmemcpy(gens, cs->gens, sizeof(u32) * cs->nslot);
  kfree(cs->slots);
  kfree(cs->gens);
  cs->slots = slots;
  cs->gens = gens;
  cs->nslot = n;
  return 0;
}

/* ---------------- CSpace ---------------- */

cspace_t* cspace_create(void) {
  cspace_t* cs = kmalloc(sizeof(cspace_t), KERNEL_TYPE);
  if (cs == NULL) {
    return NULL;
  }
  kmemset(cs, 0, sizeof(cspace_t));
  cs->slots = kmalloc(sizeof(cap_t) * CSPACE_MIN_SLOTS, KERNEL_TYPE);
  cs->gens = kmalloc(sizeof(u32) * CSPACE_MIN_SLOTS, KERNEL_TYPE);
  if (cs->slots == NULL || cs->gens == NULL) {
    if (cs->slots != NULL) kfree(cs->slots);
    if (cs->gens != NULL) kfree(cs->gens);
    kfree(cs);
    return NULL;
  }
  kmemset(cs->slots, 0, sizeof(cap_t) * CSPACE_MIN_SLOTS);
  kmemset(cs->gens, 0, sizeof(u32) * CSPACE_MIN_SLOTS);
  cs->nslot = CSPACE_MIN_SLOTS;
  cs->ref = 1;
  return cs;
}

/* 解绑该能力记录过的映射 */
static void cap_unmap_record(cap_t* c, void* pgdir) {
  if (c == NULL || c->map_va == 0 || pgdir == NULL) {
    return;
  }
  const obj_ops_t* ops = obj_ops_get(c->ty);
  if (ops != NULL && ops->unmap != NULL) {
    ops->unmap(c->obj, pgdir, c->map_va, c->map_off, c->map_len);
  }
  c->map_va = 0;
  c->map_len = 0;
  c->map_off = 0;
}

void cspace_destroy(cspace_t* cs, void* pgdir) {
  if (cs == NULL) {
    return;
  }
  for (u32 i = 1; i < cs->nslot; i++) {
    cap_t* c = &cs->slots[i];
    if (c->obj == NULL) {
      continue;
    }
    cap_unmap_record(c, pgdir);
    kobject_t* obj = c->obj;
    kmemset(c, 0, sizeof(cap_t));
    obj_put(obj);
  }
  kfree(cs->slots);
  kfree(cs->gens);
  kfree(cs);
}

cspace_t* cspace_share_or_dup(cspace_t* src, int share) {
  if (src == NULL) {
    return NULL;
  }
  if (share) {
    src->ref++;
    return src;
  }
  /* fork：复制整表。句柄值保持一致（gens 一起复制），但去掉转授/派生/撤销
   * 三类权限 —— 子进程可以用父进程给它的能力，不能替父进程再分发。 */
  cspace_t* dst = cspace_create();
  if (dst == NULL) {
    return NULL;
  }
  if (src->nslot > dst->nslot) {
    while (dst->nslot < src->nslot && dst->nslot < CSPACE_MAX_SLOTS) {
      if (cspace_grow(dst) < 0) {
        break;
      }
    }
  }
  for (u32 i = 1; i < src->nslot && i < dst->nslot; i++) {
    cap_t* s = &src->slots[i];
    dst->gens[i] = src->gens[i];
    if (s->obj == NULL) {
      continue;
    }
    cap_t* d = &dst->slots[i];
    kmemcpy(d, s, sizeof(cap_t));
    d->rights &= ~(RIGHT_GRANT | RIGHT_MINT | RIGHT_REVOKE);
    obj_ref(s->obj);
  }
  return dst;
}

void cspace_put(cspace_t* cs, void* pgdir) {
  if (cs == NULL) {
    return;
  }
  if (cs->ref > 1) {
    cs->ref--;
    return;
  }
  cspace_destroy(cs, pgdir);
}

cap_t* cspace_get(cspace_t* cs, u32 handle) {
  if (cs == NULL || handle == 0) {
    return NULL;
  }
  u32 slot = handle & CAP_SLOT_MASK;
  u32 gen = handle >> CAP_SLOT_BITS;
  if (slot == 0 || slot >= cs->nslot) {
    return NULL;
  }
  cap_t* c = &cs->slots[slot];
  if (c->obj == NULL) {
    return NULL;
  }
  if ((cs->gens[slot] & CAP_GEN_MASK) != gen) {
    return NULL; /* 槽位已复用/句柄伪造 */
  }
  return c;
}

u32 cap_install(cspace_t* cs, kobject_t* obj, u32 rights, u32 parent_slot) {
  if (cs == NULL || obj == NULL) {
    return 0;
  }
  /* 先找空槽，找不到再扩容 */
  for (;;) {
    for (u32 i = 1; i < cs->nslot; i++) {
      if (cs->slots[i].obj != NULL) {
        continue;
      }
      cs->gens[i]++;
      if ((cs->gens[i] & CAP_GEN_MASK) == 0) {
        cs->gens[i] = 1;
      }
      cap_t* c = &cs->slots[i];
      kmemset(c, 0, sizeof(cap_t));
      c->obj = obj;
      c->ty = obj->ty;
      c->rights = rights;
      c->epoch = obj->epoch;
      c->parent = parent_slot;
      c->slot = i;
      return cap_handle_of(cs, i);
    }
    if (cs->nslot >= CSPACE_MAX_SLOTS || cspace_grow(cs) < 0) {
      log_error("cap: cspace full (%d slots)\n", cs->nslot);
      return 0;
    }
  }
}

/* ---------------- 原语 ---------------- */

int cap_copy(cspace_t* src, u32 src_h, cspace_t* dst, u32* out_h) {
  cap_t* c = cspace_get(src, src_h);
  if (c == NULL) {
    return -1;
  }
  if ((c->rights & RIGHT_GRANT) == 0) {
    return -1; /* 无转授权 */
  }
  if (dst == NULL) {
    return -1;
  }
  obj_ref(c->obj);
  u32 nh = cap_install(dst, c->obj, c->rights, 0);
  if (nh == 0) {
    obj_put(c->obj);
    return -1;
  }
  if (out_h != NULL) {
    *out_h = nh;
  }
  return 0;
}

int cap_move(cspace_t* src, u32 src_h, cspace_t* dst, u32* out_h) {
  cap_t* c = cspace_get(src, src_h);
  if (c == NULL || dst == NULL) {
    return -1;
  }
  u32 nh = cap_install(dst, c->obj, c->rights, 0);
  if (nh == 0) {
    return -1;
  }
  /* 源槽清空（gen++ 让旧句柄失效），对象引用转给新槽，不做 obj_put */
  u32 slot = c->slot;
  kmemset(c, 0, sizeof(cap_t));
  src->gens[slot]++;
  if ((src->gens[slot] & CAP_GEN_MASK) == 0) {
    src->gens[slot] = 1;
  }
  if (out_h != NULL) {
    *out_h = nh;
  }
  return 0;
}

u32 cap_mint(cspace_t* cs, u32 h, u32 rights) {
  cap_t* c = cspace_get(cs, h);
  if (c == NULL) {
    return 0;
  }
  if ((c->rights & RIGHT_MINT) == 0) {
    return 0;
  }
  u32 nr = c->rights & rights; /* 权限单调不增 */
  obj_ref(c->obj);
  u32 nh = cap_install(cs, c->obj, nr, c->slot);
  if (nh == 0) {
    obj_put(c->obj);
  }
  return nh;
}

void cap_delete(cspace_t* cs, u32 h, void* pgdir) {
  cap_t* c = cspace_get(cs, h);
  if (c == NULL) {
    return;
  }
  cap_unmap_record(c, pgdir);
  u32 slot = c->slot;
  kobject_t* obj = c->obj;
  kmemset(c, 0, sizeof(cap_t));
  cs->gens[slot]++;
  if ((cs->gens[slot] & CAP_GEN_MASK) == 0) {
    cs->gens[slot] = 1;
  }
  obj_put(obj);
}

/* slot 的 parent 链上是否经过 root（不含自身） */
static int descends_from(cspace_t* cs, u32 slot, u32 root) {
  u32 p = cs->slots[slot].parent;
  u32 guard = 0;
  while (p != 0 && p < cs->nslot && guard++ < CSPACE_MAX_SLOTS) {
    if (p == root) {
      return 1;
    }
    p = cs->slots[p].parent;
  }
  return 0;
}

void cap_revoke(cspace_t* cs, u32 h, void* pgdir) {
  cap_t* self = cspace_get(cs, h);
  if (self == NULL) {
    return;
  }
  if ((self->rights & RIGHT_REVOKE) == 0) {
    return;
  }
  u32 root = self->slot;
  /* 先撤派生能力，再删自己（seL4 的 revoke 也常与 delete 连着用） */
  for (u32 i = 1; i < cs->nslot; i++) {
    if (i == root || cs->slots[i].obj == NULL) {
      continue;
    }
    if (!descends_from(cs, i, root)) {
      continue;
    }
    u32 hh = cap_handle_slot(cs, i);
    if (hh != 0) {
      cap_delete(cs, hh, pgdir);
    }
  }
  cap_delete(cs, h, pgdir);
}

int cap_invoke(cspace_t* cs, u32 h, void* arg) {
  cap_t* c = cspace_get(cs, h);
  if (c == NULL) {
    return -1; /* -CAP_NIL */
  }
  const obj_ops_t* ops = obj_ops_get(c->ty);
  if (ops == NULL) {
    return -2; /* -CAP_TYPE */
  }
  if ((c->rights & RIGHT_INVOKE) == 0) {
    return -3; /* -CAP_RIGHTS */
  }
  if (c->obj == NULL || c->obj->epoch != c->epoch) {
    return -4; /* -CAP_STALE */
  }
  if (ops->invoke == NULL) {
    return -5;
  }
  return ops->invoke(c->obj, c, arg);
}

int cap_info(cspace_t* cs, u32 h, u32* ty, u32* rights, u32* obj_id,
             u64* epoch) {
  cap_t* c = cspace_get(cs, h);
  if (c == NULL) {
    return -1;
  }
  if (ty != NULL) *ty = c->ty;
  if (rights != NULL) *rights = c->rights;
  if (obj_id != NULL) *obj_id = (c->obj != NULL) ? c->obj->id : 0;
  if (epoch != NULL) *epoch = c->epoch;
  return 0;
}

cspace_t* cap_current(void) {
  thread_t* t = thread_current();
  if (t == NULL) {
    return NULL;
  }
  /* 惰性创建：首个用户线程（由内核线程 fork 出来时已经建好，这里兜底），
   * 避免每个进程启动路径都要记得调一次 cspace_create。 */
  if (t->cspace == NULL) {
    t->cspace = cspace_create();
  }
  return t->cspace;
}

int cap_range_owner(cspace_t* cs, vaddr_t pa, vaddr_t size, u32 need_rights) {
  if (cs == NULL) {
    return 0; /* 内核线程没有 CSpace，由调用方决定放行策略 */
  }
  if (size == 0) {
    size = PAGE_SIZE;
  }
  for (u32 i = 1; i < cs->nslot; i++) {
    cap_t* c = &cs->slots[i];
    if (c->obj == NULL || c->obj->state == NULL) {
      continue;
    }
    if (c->ty == OBJ_DEVWIN) {
      if ((c->rights & need_rights) != need_rights) {
        continue;
      }
      const foot_dev_win_t* win = (const foot_dev_win_t*)c->obj->state;
      if (pa >= win->base && (pa + size) <= (win->base + win->size)) {
        return 1;
      }
    } else if (c->ty == OBJ_FRAME) {
      /* 帧只认 FRAME_MAP：调用方要的映射权必须落在该能力上 */
      if ((need_rights & FRAME_MAP) != 0 && (c->rights & FRAME_MAP) == 0) {
        continue;
      }
      const foot_frame_t* f = (const foot_frame_t*)c->obj->state;
      if (pa >= f->pa && (pa + size) <= (f->pa + f->size)) {
        return 2;
      }
    } else if (c->ty == OBJ_ARENA) {
      /* arena 与 frame 同语义（物理内存覆盖），也归 CAP_RANGE_FRAME */
      if ((need_rights & ARENA_MAP) != 0 && (c->rights & ARENA_MAP) == 0) {
        continue;
      }
      const foot_arena_t* a = (const foot_arena_t*)c->obj->state;
      if (pa >= a->pa && (pa + size) <= (a->pa + a->size)) {
        return 2;
      }
    }
  }
  return 0;
}

int cap_check_phys_range(cspace_t* cs, vaddr_t pa, vaddr_t size,
                         u32 need_rights) {
  return cap_range_owner(cs, pa, size, need_rights) != 0;
}

/* ---------------- 类型无关的用法 ---------------- */

/* 该类型"映射"需要哪个类型位 */
static u32 cap_map_right(obj_type_t ty) {
  switch (ty) {
    case OBJ_DEVWIN:
      return DEVWIN_MAP;
    case OBJ_FRAME:
      return FRAME_MAP;
    case OBJ_ARENA:
      return ARENA_MAP;
    default:
      return 0;
  }
}

int cap_map(cspace_t* cs, u32 h, void* pgdir, vaddr_t vaddr, vaddr_t offset,
            vaddr_t len) {
  cap_t* c = cspace_get(cs, h);
  if (c == NULL || c->obj == NULL) {
    return -1;
  }
  u32 need = cap_map_right(c->ty);
  if (need == 0 || (c->rights & need) == 0) {
    return -1;
  }
  const obj_ops_t* ops = obj_ops_get(c->ty);
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

int cap_unmap(cspace_t* cs, u32 h, void* pgdir) {
  cap_t* c = cspace_get(cs, h);
  if (c == NULL || c->obj == NULL) {
    return -1;
  }
  u32 need = cap_map_right(c->ty);
  if (need == 0 || (c->rights & need) == 0) {
    return -1;
  }
  cap_unmap_record(c, pgdir);
  return 0;
}

int cap_res_info(cspace_t* cs, u32 h, u32* pa, u32* size, u32* rights) {
  cap_t* c = cspace_get(cs, h);
  if (c == NULL) {
    return -1;
  }
  switch (c->ty) {
    case OBJ_DEVWIN:
      return cap_devwin_info(cs, h, pa, size, rights);
    case OBJ_FRAME:
      return cap_frame_info(cs, h, pa, size, rights);
    case OBJ_ARENA:
      return cap_arena_info(cs, h, pa, size, rights);
    default:
      return -1;
  }
}
