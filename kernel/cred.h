/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef KERNEL_CRED_H
#define KERNEL_CRED_H

#include "kernel/kernel.h"

/* ============ 进程凭据（用户权限模型的基础）============
 * Linux 的凭据是"线程组"级：setuid 影响同进程所有线程，fork 复制，exec
 * 保留。本内核没有 thread group 结构，vm 与进程一一对应，所以挂
 * vmemory_t（与 cap 表同层）：fork 复制一份、CLONE_VM 共享、退出释放。
 *
 * 权限规则（POSIX 简化版）：
 *   · euid == 0（root）可以任意切换；
 *   · 否则只能在 uid/euid/suid 三个身份之间切，不能凭空提权；
 *   · setgroups 仅 root 可调用。
 * 返回 -1 表示 EPERM（musl 会把内核的 -1 映射成 EPERM）。 */

#define CRED_NOCHG 0xFFFFFFFFu /* setresuid/setresgid 的"保持不变" */
#define CRED_MAX_GROUPS 8

typedef struct cred {
  u32 uid;   /* 真实用户 id */
  u32 euid;  /* 有效用户 id：权限判定看它 */
  u32 suid;  /* 保存的用户 id：允许降权后恢复 */
  u32 gid;
  u32 egid;
  u32 sgid;
  u16 ngroups;
  u32 groups[CRED_MAX_GROUPS];
  u32 ref; /* 引用计数：vm 初始化/销毁时增减 */
} cred_t;

/* 全局 root 凭据（uid=0）。vm 初始化时引用它。 */
cred_t* cred_root(void);
/* 复制一份（fork 用），返回独立引用。 */
cred_t* cred_dup(cred_t* src);
void cred_ref(cred_t* c);
void cred_free(cred_t* c);

/* 当前进程凭据（thread_current()->vm->cred） */
cred_t* cred_current(void);
int cred_is_root(cred_t* c);

/* 切换身份：成功 0，EPERM 返回 -1 */
int cred_setuid(cred_t* c, u32 uid);
int cred_setgid(cred_t* c, u32 gid);
int cred_setresuid(cred_t* c, u32 r, u32 e, u32 s);
int cred_setresgid(cred_t* c, u32 r, u32 e, u32 s);
int cred_setgroups(cred_t* c, int n, const u32* list);

#endif
