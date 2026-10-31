/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "cred.h"

#include "memory.h"
#include "thread.h"

#define log_debug

/* 全局 root 凭据：所有初始进程共享一份（只读为主，切换身份时 fork 出副本
 * 才有独立 cred —— 见 cred_dup 与 vmemory_clone）。 */
static cred_t g_root_cred;
static int g_root_ready = 0;

cred_t* cred_root(void) {
  if (!g_root_ready) {
    kmemset(&g_root_cred, 0, sizeof(g_root_cred));
    g_root_cred.uid = 0;
    g_root_cred.euid = 0;
    g_root_cred.suid = 0;
    g_root_cred.gid = 0;
    g_root_cred.egid = 0;
    g_root_cred.sgid = 0;
    g_root_cred.ref = 1;
    g_root_ready = 1;
  }
  return &g_root_cred;
}

void cred_ref(cred_t* c) {
  if (c != NULL && c != &g_root_cred) {
    c->ref++;
  }
}

void cred_free(cred_t* c) {
  if (c == NULL || c == &g_root_cred) {
    return; /* root 常驻，不随进程退出释放 */
  }
  if (c->ref > 1) {
    c->ref--;
    return;
  }
  kfree(c);
}

cred_t* cred_dup(cred_t* src) {
  cred_t* c = kmalloc(sizeof(cred_t), KERNEL_TYPE);
  if (c == NULL) {
    return NULL;
  }
  cred_t* from = (src != NULL) ? src : cred_root();
  kmemcpy(c, from, sizeof(cred_t));
  c->ref = 1;
  return c;
}

cred_t* cred_current(void) {
  thread_t* current = thread_current();
  if (current == NULL) {
    return NULL;
  }
  return current->cred; /* 凭据挂 TCB：fork 复制、CLONE_VM 共享同一份 */
}

int cred_is_root(cred_t* c) {
  return c != NULL && c->euid == 0;
}

int cred_setuid(cred_t* c, u32 uid) {
  if (c == NULL) {
    return -1;
  }
  if (c->euid == 0) {
    /* root：真实/有效/保存三位一起设（POSIX 对 root 的简化语义） */
    c->uid = uid;
    c->euid = uid;
    c->suid = uid;
    log_debug("cred: root setuid %d\n", uid);
    return 0;
  }
  /* 非 root：只能切到已有身份（uid 或 suid），不能凭空提权 */
  if (uid == c->uid || uid == c->suid) {
    c->euid = uid;
    return 0;
  }
  log_warn("cred: setuid %d denied (uid %d euid %d suid %d)\n", uid, c->uid,
           c->euid, c->suid);
  return -1;
}

int cred_setgid(cred_t* c, u32 gid) {
  if (c == NULL) {
    return -1;
  }
  if (c->euid == 0) {
    c->gid = gid;
    c->egid = gid;
    c->sgid = gid;
    return 0;
  }
  if (gid == c->gid || gid == c->sgid) {
    c->egid = gid;
    return 0;
  }
  log_warn("cred: setgid %d denied\n", gid);
  return -1;
}

/* setresuid(r, e, s)：0xFFFFFFFF = 保持不变。
 * 非 root 时每个要改的值都必须落在 {uid, euid, suid} 里。 */
static int cred_uid_allowed(cred_t* c, u32 v) {
  return v == CRED_NOCHG || c->euid == 0 || v == c->uid || v == c->euid ||
         v == c->suid;
}

int cred_setresuid(cred_t* c, u32 r, u32 e, u32 s) {
  if (c == NULL) {
    return -1;
  }
  if (!cred_uid_allowed(c, r) || !cred_uid_allowed(c, e) ||
      !cred_uid_allowed(c, s)) {
    log_warn("cred: setresuid(%d,%d,%d) denied\n", r, e, s);
    return -1;
  }
  if (r != CRED_NOCHG) c->uid = r;
  if (e != CRED_NOCHG) c->euid = e;
  if (s != CRED_NOCHG) c->suid = s;
  return 0;
}

static int cred_gid_allowed(cred_t* c, u32 v) {
  return v == CRED_NOCHG || c->euid == 0 || v == c->gid || v == c->egid ||
         v == c->sgid;
}

int cred_setresgid(cred_t* c, u32 r, u32 e, u32 s) {
  if (c == NULL) {
    return -1;
  }
  if (!cred_gid_allowed(c, r) || !cred_gid_allowed(c, e) ||
      !cred_gid_allowed(c, s)) {
    log_warn("cred: setresgid(%d,%d,%d) denied\n", r, e, s);
    return -1;
  }
  if (r != CRED_NOCHG) c->gid = r;
  if (e != CRED_NOCHG) c->egid = e;
  if (s != CRED_NOCHG) c->sgid = s;
  return 0;
}

int cred_setgroups(cred_t* c, int n, const u32* list) {
  if (c == NULL || n < 0) {
    return -1;
  }
  if (c->euid != 0) {
    log_warn("cred: setgroups denied for uid %d\n", c->euid);
    return -1;
  }
  if (n > CRED_MAX_GROUPS) {
    n = CRED_MAX_GROUPS;
  }
  if (n > 0) {
    if (list == NULL) {
      return -1;
    }
    if (kmemcpy(c->groups, list, (size_t)n * sizeof(u32)) == NULL) {
      return -1;
    }
  }
  c->ngroups = (u16)n;
  return 0;
}
