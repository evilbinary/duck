/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef PIPE_H
#define PIPE_H

#include "kernel/kernel.h"
#include "algorithm/buffer.h"
#include "queue/rw_queue.h"

#define PIPE_BUFFER 128
#define PIPE_WAIT_QUEUE_SIZE 20

typedef struct pipe{
    rw_queue_t* wait_queue;
    buffer_t *buffer;
}pipe_t;

pipe_t* pipe_create(u32 size);

vnode_t* pipe_make(u32 size);

#endif