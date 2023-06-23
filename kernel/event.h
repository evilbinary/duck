/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef EVENT_H
#define EVENT_H

#include "kernel/thread.h"
#include "vfs.h"
#include "fd.h"


#define EVENT_SOURCE_IO 1
#define MAX_EVENT 512

typedef struct thread thread_t;

typedef struct source {
  vnode_t* node;
  fd_t* f;
  int nbytes;
  char* buf;
  u32 type;
} source_t;

typedef struct event {
  source_t* source;
  thread_t* target;
} event_t;


void event_init();

source_t* event_source_io_create(vnode_t* node, fd_t* f, int nbytes,
                                 char* buf);

void event_wait(thread_t* t, source_t* source);                                 

void event_wakeup_io(event_t* event);

#endif