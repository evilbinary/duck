/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef __SERVICE_H__
#define __SERVICE_H__

#include "kernel/kernel.h"

enum {
  DT_UNKNOWN = 0,
#define DT_UNKNOWN DT_UNKNOWN
  DT_FIFO = 1,
#define DT_FIFO DT_FIFO
  DT_CHR = 2,
#define DT_CHR DT_CHR
  DT_DIR = 4,
#define DT_DIR DT_DIR
  DT_BLK = 6,
#define DT_BLK DT_BLK
  DT_REG = 8,
#define DT_REG DT_REG
  DT_LNK = 10,
#define DT_LNK DT_LNK
  DT_SOCK = 12,
#define DT_SOCK DT_SOCK
  DT_WHT = 14
#define DT_WHT DT_WHT
};

#define MAX_CLIENTS 100
#define MAX_API_SLOTS 64     /* 每个 client 最多 64 个并发请求槽 */
#define MAX_ARGS_BUF 240     /* 单个请求的 payload 上限（字节） */
#define MAX_INSTANCE 16
#define FOOT_API_DEFAULT 4   /* 默认槽位数 */
#define FOOT_SLOT_LEAK_MS 5000 /* 调用方崩溃后槽位回收阈值（ms） */

/* 共享页控制区：紧跟在槽数组后面，与 eggs/libfoot/client.h 同布局。
 *   [0] kick  调用方的提交计数（服务端空闲时 futex 睡在这个字上）
 *   [1..3]    预留 */
#define FOOT_CTL_WORDS 4
#define FOOT_CTL_BYTES (FOOT_CTL_WORDS * 4)

enum {
  SYS_NEW_CLIENT = 1,
  SYS_GET_CLIENT = 2,
  SYS_DEL_CLIENT = 3,
  SYS_GET_CLIENT_BY_ID = 4,
};

enum {
  SYS_INIT = 0,
  SYS_RUNNING = 1,
  SYS_BLOCKING = 2,
  SYS_WAIT = 3,
  SYS_DINIT = 4,
};

enum {
  API_INIT = 0,
  API_REDAY = 1,
  API_RETURN = 2,
  API_FINISH = 3
};

/* 共享页里的一个请求槽。
 * 【重要】本结构与 eggs/libfoot/client.h 的同名结构必须逐字段一致：
 * 内核与用户态各自持有一份定义（刻意解耦），字段变更时两侧同批修改。
 *
 * 生命周期：cid==0 表示空闲；调用方用 CAS 把 cid 从 0 改成 seq 完成占用，
 * 然后写参数、置 state=API_REDAY；服务方处理完置 state=API_RETURN；
 * 调用方取走结果后把 state 置回 API_INIT、cid 置回 0。 */
typedef struct api {
  int cid;                 /* 0 = 空闲，非 0 = 正在处理（值即 seq） */
  int state;               /* API_INIT / API_REDAY / API_RETURN / API_FINISH */
  int err;                 /* 响应错误码：0 = OK */
  unsigned int seq;        /* 请求序号，用于识别陈旧响应 */
  unsigned int claim_ms;   /* 调用方写入的起始时刻（ms），服务方据此回收僵尸槽 */
  unsigned int len;        /* args 有效长度（0..MAX_ARGS_BUF） */
  unsigned int flags;      /* 保留（同步/异步、唤醒策略等） */
  void* fn;                /* 方法号 */
  void* ret;               /* 返回值 */
  char args[MAX_ARGS_BUF]; /* 参数区（按 len 拷贝，未用部分为 0） */
} api_t;

typedef struct client {
  char* name;
  int tid;
  int id;
  int cid;
  int token;
  int fd;
  int service_id;
  int type;
  int api_size; /* 槽位数，由服务方注册时声明（1..MAX_API_SLOTS） */
  api_t* apis;  /* 指向共享页（服务方与调用方映射同一物理页） */
  int state;
  int count;
  struct client* instance[MAX_INSTANCE];
} client_t;

typedef struct client_ctl {
  char* name;
  int id;
  int api_size; /* SYS_NEW_CLIENT：期望槽位数；返回时回填实际值 */
  client_t* client;
} client_ctl_t;

u32 service_open(vnode_t* node, u32 mode);

u32 service_close(vnode_t* node);

size_t service_read(vnode_t* node, void* buf, size_t len);

size_t service_write(vnode_t* node, const void* buf, size_t len);

ssize_t service_ioctl(vnode_t* node, u32 cmd, void* args);

vnode_t* service_find(vnode_t*, char* name);
u32 service_readdir(vnode_t* node, struct vdirent* dirent, u32* offset,
                    u32 count);
#endif
