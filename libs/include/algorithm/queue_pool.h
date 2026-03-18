/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * 数据队列 - 基于 ring_queue 的兼容层
 ********************************************************************/
#ifndef QUEUE_POOL_H
#define QUEUE_POOL_H

#include "types.h"

// 类型别名
typedef struct ring_queue queue_pool_t;
typedef struct ring_queue ring_queue_t;

// 兼容旧 API（实际函数在 queue_pool.c 中实现）
queue_pool_t* queue_pool_create(u32 capacity, u32 elem_size);
queue_pool_t* queue_pool_create_align(u32 capacity, u32 elem_size, u32 align);
int queue_pool_put(queue_pool_t* q, void* elem);
int queue_pool_poll(queue_pool_t* q, void* elem);
void queue_pool_destroy(queue_pool_t* q);
u32 queue_pool_count(queue_pool_t* q);
u32 queue_pool_is_empty(queue_pool_t* q);
u32 queue_pool_is_full(queue_pool_t* q);
void queue_pool_clear(queue_pool_t* q);

// ring_queue API（与 queue_pool 共享实现）
ring_queue_t* ring_queue_create(u32 capacity, u32 elem_size);
ring_queue_t* ring_queue_create_align(u32 capacity, u32 elem_size, u32 align);
void ring_queue_destroy(ring_queue_t* q);
int ring_queue_put(ring_queue_t* q, void* elem);
int ring_queue_poll(ring_queue_t* q, void* elem);
int ring_queue_peek(ring_queue_t* q, void* elem);
u32 ring_queue_is_empty(ring_queue_t* q);
u32 ring_queue_is_full(ring_queue_t* q);
u32 ring_queue_count(ring_queue_t* q);
void ring_queue_clear(ring_queue_t* q);

#endif
