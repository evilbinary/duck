/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 *
 * capability v2 的对象层：全局对象表 + 类型注册表 + epoch 生命周期。
 * 对象是"被能力指向的实体"（设备窗口 / 帧 / 端点 / IRQ / untyped），
 * 由 obj_ops 解释具体语义。新增资源类型只注册 ops，不改本文件与 cap.c。
 ********************************************************************/
#include "cap.h"

#include "memory.h"

#define log_debug

/* 对象表：静态 256 槽，id = 槽号 + 1（0 保留给"无对象"）。
 * 用静态表而不是 kmalloc：对象本身是内核资源，生命周期比任何线程都长，
 * 也避免在撤销/退出路径里再走一次分配器。 */
static kobject_t g_objtab[OBJ_MAX];
static const obj_ops_t* g_ops[OBJ_TYPE_MAX];

void obj_init(void) {
  for (u32 i = 0; i < OBJ_MAX; i++) {
    g_objtab[i].ty = OBJ_NONE;
    g_objtab[i].id = 0;
    g_objtab[i].state = NULL;
    g_objtab[i].refs = 0;
    /* epoch 不清零：槽位复用后继续递增，旧能力自然 stale */
  }
  for (u32 i = 0; i < OBJ_TYPE_MAX; i++) {
    g_ops[i] = NULL;
  }
}

int obj_register(const obj_ops_t* ops) {
  if (ops == NULL || ops->ty <= OBJ_NONE || ops->ty >= OBJ_TYPE_MAX) {
    return -1;
  }
  if (g_ops[ops->ty] != NULL) {
    log_warn("obj: type %d already registered by %s\n", ops->ty,
             g_ops[ops->ty]->name);
    return -1;
  }
  g_ops[ops->ty] = ops;
  return 0;
}

const obj_ops_t* obj_ops_get(obj_type_t ty) {
  if (ty <= OBJ_NONE || ty >= OBJ_TYPE_MAX) {
    return NULL;
  }
  return g_ops[ty];
}

kobject_t* obj_create(obj_type_t ty, void* state) {
  if (ty <= OBJ_NONE || ty >= OBJ_TYPE_MAX) {
    return NULL;
  }
  for (u32 i = 0; i < OBJ_MAX; i++) {
    kobject_t* o = &g_objtab[i];
    if (o->state != NULL || o->refs != 0) {
      continue; /* 在用 */
    }
    o->ty = ty;
    o->id = i + 1;
    o->state = state;
    o->refs = 1; /* 属于"第一个"能力，由 cap_install 的调用方移交 */
    log_debug("obj create id=%d ty=%d state=%x\n", o->id, ty, (u32)state);
    return o;
  }
  log_error("obj: table full (%d)\n", OBJ_MAX);
  return NULL;
}

kobject_t* obj_get(u32 id) {
  if (id == 0 || id > OBJ_MAX) {
    return NULL;
  }
  kobject_t* o = &g_objtab[id - 1];
  if (o->refs == 0 || o->state == NULL) {
    return NULL;
  }
  return o;
}

void obj_ref(kobject_t* o) {
  if (o != NULL) {
    o->refs++;
  }
}

void obj_put(kobject_t* o) {
  if (o == NULL) {
    return;
  }
  if (o->refs == 0) {
    log_error("obj: put with refs=0 id=%d ty=%d\n", o->id, o->ty);
    return;
  }
  o->refs--;
  if (o->refs > 0) {
    return;
  }
  const obj_ops_t* ops = obj_ops_get(o->ty);
  if (ops != NULL && ops->destroy != NULL) {
    ops->destroy(o);
  }
  log_debug("obj destroy id=%d ty=%d epoch=%d\n", o->id, o->ty,
            (u32)o->epoch);
  o->state = NULL;
  o->ty = OBJ_NONE;
  o->epoch++; /* 旧能力（复制了旧 epoch）从此 stale */
}

kobject_t* obj_find(obj_type_t ty, int (*match)(kobject_t*, const char*),
                    const char* arg) {
  for (u32 i = 0; i < OBJ_MAX; i++) {
    kobject_t* o = &g_objtab[i];
    if (o->state == NULL || o->refs == 0 || o->ty != ty) {
      continue;
    }
    if (match == NULL || match(o, arg)) {
      return o;
    }
  }
  return NULL;
}
