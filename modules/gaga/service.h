/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef __SERVICE_H__
#define __SERVICE__

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

#define MAX_ARGS_BUF 128
#define MAX_INSTANCE 16

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

enum{
  API_INIT =0,
  API_REDAY=1,
  API_RETURN=2,
  API_FINISH=3
};

typedef struct api {
  int cid;        // call id
  int state;         // api state
  char* fn;       // function id
  void* ret;      // return value
  char args[MAX_ARGS_BUF];  // params
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
  int api_size;
  api_t* apis;
  int state;
  int count;
  struct client *instance[MAX_INSTANCE];
} client_t;

typedef struct client_ctl {
  char* name;
  int id;
  client_t* client;
} client_ctl_t;

u32 service_open(vnode_t* node, u32 mode);

u32 service_close(vnode_t* node);

size_t service_read(vnode_t* node, void* buf, size_t len);

size_t service_write(vnode_t* node, const void* buf, size_t len);

size_t service_ioctl(vnode_t* node, u32 cmd, void* args);

vnode_t* service_find(vnode_t*, char* name);
u32 service_readdir(vnode_t* node, struct vdirent* dirent, u32* offset,
                    u32 count);
#endif