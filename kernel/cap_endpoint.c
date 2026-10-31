/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 *
 * OBJ_ENDPOINT：IPC 端点（内核消息队列）与随消息转授能力。
 *
 * 一条消息 = 小段数据 + 一个可选的"随消息携带的能力"。
 * 带能力的那条消息在内核里持有一份对象引用，接收方取走时这份引用
 * 直接装进它的 CSpace —— 这就是"父进程把自己的能力交给子进程"的通路。
 ********************************************************************/
#include "cap.h"

#include "memory.h"
#include "thread.h"

#define log_debug

/* 队列状态变化后叫醒阻塞在收/发上的线程。
 * 用 thread_wait_notify_hook 而不是直接调 poll_wait_notify：端点在内核核心，
 * 不能反向依赖 posix 模块（钩子由 poll_wait_init 装上，未装时为空）。
 * 【防递归】唤醒方在重查（recheck）里调用本文件的 ex 版本，wake 传 0，
 * 否则 poll_wait_notify → recheck → send → notify 会自递归。 */
static void ep_wake(void) {
  if (thread_wait_notify_hook != NULL) {
    thread_wait_notify_hook();
  }
}

typedef struct cap_ep_msg {
  u32 len;              /* 数据长度 */
  u32 grant_rights;     /* 转授权限（grant_obj 非空时有效） */
  kobject_t* grant_obj; /* 内核持有的那份引用 */
  u8 data[CAP_EP_DATA_MAX];
} cap_ep_msg_t;

typedef struct foot_endpoint {
  u32 head; /* 出队位置 */
  u32 tail; /* 入队位置 */
  u32 cnt;  /* 当前消息数 */
  u32 owner;
  cap_ep_msg_t msgs[CAP_EP_MSGS];
} foot_endpoint_t;

/* ---------------- ops ---------------- */

static void endpoint_destroy(kobject_t* obj) {
  if (obj == NULL || obj->state == NULL) {
    return;
  }
  foot_endpoint_t* ep = (foot_endpoint_t*)obj->state;
  /* 队列里还没被取走的能力引用必须还回去，否则对象永远不会销毁 */
  for (u32 i = 0; i < CAP_EP_MSGS; i++) {
    if (ep->msgs[i].grant_obj != NULL) {
      obj_put(ep->msgs[i].grant_obj);
      ep->msgs[i].grant_obj = NULL;
    }
  }
  kfree(ep);
  obj->state = NULL;
}

static int endpoint_invoke(kobject_t* obj, cap_t* c, void* arg) {
  (void)obj;
  (void)c;
  (void)arg;
  return -1; /* 收发走 cap_endpoint_send/recv（要访问调用方 CSpace） */
}

static const obj_ops_t endpoint_ops = {
    .ty = OBJ_ENDPOINT,
    .name = "endpoint",
    .invoke = endpoint_invoke,
    .destroy = endpoint_destroy,
    .map = NULL,
    .unmap = NULL,
};

/* ---------------- 对外 ---------------- */

void cap_endpoint_init(void) { obj_register(&endpoint_ops); }

u32 cap_endpoint_create(cspace_t* cs) {
  if (cs == NULL) {
    return 0;
  }
  thread_t* t = thread_current();
  foot_endpoint_t* ep = kmalloc(sizeof(foot_endpoint_t), KERNEL_TYPE);
  if (ep == NULL) {
    return 0;
  }
  kmemset(ep, 0, sizeof(foot_endpoint_t));
  ep->owner = (t != NULL) ? t->id : 0;

  kobject_t* o = obj_create(OBJ_ENDPOINT, ep);
  if (o == NULL) {
    kfree(ep);
    return 0;
  }
  u32 h = cap_install(cs, o, CAP_ENDPOINT_RIGHTS_DEFAULT, 0);
  if (h == 0) {
    obj_put(o); /* 归零 → endpoint_destroy 释放队列与残留引用 */
    return 0;
  }
  return h;
}

int cap_endpoint_send(cspace_t* cs, u32 h, const void* data, u32 len,
                      u32 grant_h, u32 grant_rights) {
  return cap_endpoint_send_ex(cs, h, data, len, grant_h, grant_rights, 1);
}

int cap_endpoint_send_ex(cspace_t* cs, u32 h, const void* data, u32 len,
                         u32 grant_h, u32 grant_rights, int wake) {
  if (len > CAP_EP_DATA_MAX) {
    return -1;
  }
  cap_t* c = cspace_get(cs, h);
  if (c == NULL || c->ty != OBJ_ENDPOINT || c->obj == NULL ||
      c->obj->state == NULL) {
    return -1;
  }
  if ((c->rights & EP_SEND) == 0) {
    return -1;
  }
  /* 先校验转授能力，再动队列，避免"对象已 ref 但消息入不了队"的泄漏 */
  kobject_t* grant_obj = NULL;
  if (grant_h != 0) {
    cap_t* g = cspace_get(cs, grant_h);
    if (g == NULL || g->obj == NULL) {
      return -1;
    }
    if ((g->rights & RIGHT_GRANT) == 0) {
      return -1; /* 没有转授权，不能把别人的能力送出去 */
    }
    grant_obj = g->obj;
    if (grant_rights == 0) {
      /* 默认按 fork 的语义给：能用，但不能替发送方再分发/撤销 */
      grant_rights =
          g->rights & ~(RIGHT_GRANT | RIGHT_MINT | RIGHT_REVOKE);
    } else {
      grant_rights &= g->rights; /* 权限单调不增 */
    }
  }

  foot_endpoint_t* ep = (foot_endpoint_t*)c->obj->state;
  if (ep->cnt >= CAP_EP_MSGS) {
    return -1; /* 队列满：非阻塞，调用方轮询 */
  }
  cap_ep_msg_t* m = &ep->msgs[ep->tail];
  kmemset(m, 0, sizeof(cap_ep_msg_t));
  m->len = len;
  if (len > 0 && data != NULL) {
    kmemcpy(m->data, data, len);
  }
  if (grant_obj != NULL) {
    obj_ref(grant_obj); /* 队列持有的一份，接收方取走时转移 */
    m->grant_obj = grant_obj;
    m->grant_rights = grant_rights;
  }
  ep->tail = (ep->tail + 1) % CAP_EP_MSGS;
  ep->cnt++;
  log_debug("ep send h=%x len=%d grant=%d cnt=%d\n", h, len,
            grant_obj != NULL, ep->cnt);
  /* 可能有线程正阻塞在 recv 上（等这条消息） */
  if (wake) {
    ep_wake();
  }
  return 0;
}

int cap_endpoint_recv(cspace_t* cs, u32 h, void* data, u32 cap, u32* out_len,
                      u32* out_grant) {
  return cap_endpoint_recv_ex(cs, h, data, cap, out_len, out_grant, 1);
}

int cap_endpoint_recv_ex(cspace_t* cs, u32 h, void* data, u32 cap, u32* out_len,
                         u32* out_grant, int wake) {
  cap_t* c = cspace_get(cs, h);
  if (c == NULL || c->ty != OBJ_ENDPOINT || c->obj == NULL ||
      c->obj->state == NULL) {
    return -1;
  }
  if ((c->rights & EP_RECV) == 0) {
    return -1;
  }
  foot_endpoint_t* ep = (foot_endpoint_t*)c->obj->state;
  if (ep->cnt == 0) {
    return -1; /* 空队列：非阻塞 */
  }
  cap_ep_msg_t* m = &ep->msgs[ep->head];
  u32 n = m->len;
  if (n > cap) {
    n = cap; /* 短读，剩余丢弃：消息是小段控制信息，不做流语义 */
  }
  if (n > 0 && data != NULL) {
    kmemcpy(data, m->data, n);
  }
  if (out_len != NULL) {
    *out_len = n;
  }
  /* 能力引用从队列转到接收者 CSpace（不额外 obj_ref，引用整体转移） */
  u32 nh = 0;
  if (m->grant_obj != NULL) {
    nh = cap_install(cs, m->grant_obj, m->grant_rights, 0);
    if (nh == 0) {
      obj_put(m->grant_obj); /* 装不下就丢弃，避免泄漏 */
    }
    m->grant_obj = NULL;
  }
  if (out_grant != NULL) {
    *out_grant = nh;
  }
  kmemset(m, 0, sizeof(cap_ep_msg_t));
  ep->head = (ep->head + 1) % CAP_EP_MSGS;
  ep->cnt--;
  log_debug("ep recv h=%x len=%d grant=%d cnt=%d\n", h, n, nh, ep->cnt);
  /* 腾出空位，叫醒阻塞在 send 上的线程 */
  if (wake) {
    ep_wake();
  }
  return 0;
}
