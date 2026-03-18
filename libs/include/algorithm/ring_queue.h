/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 * 环形队列 - 直接存储数据（非指针）
 ********************************************************************/
#ifndef RING_QUEUE_H
#define RING_QUEUE_H

#include "types.h"
#include "fun_define.h"

typedef struct ring_queue {
    u8* buffer;       // 数据缓冲区
    u32 capacity;     // 容量（元素个数）
    u32 elem_size;    // 每个元素大小
    u32 head;         // 读指针
    u32 tail;         // 写指针
    u32 count;        // 当前元素数
} ring_queue_t;

// 创建环形队列
ring_queue_t* ring_queue_create(u32 capacity, u32 elem_size);

// 创建带对齐的环形队列
ring_queue_t* ring_queue_create_align(u32 capacity, u32 elem_size, u32 align);

// 销毁队列
void ring_queue_destroy(ring_queue_t* q);

// 入队（拷贝数据）
int ring_queue_put(ring_queue_t* q, void* elem);

// 出队（拷贝数据到 elem）
int ring_queue_poll(ring_queue_t* q, void* elem);

// 查看队首元素（不移除）
int ring_queue_peek(ring_queue_t* q, void* elem);

// 判断是否为空
u32 ring_queue_is_empty(ring_queue_t* q);

// 判断是否已满
u32 ring_queue_is_full(ring_queue_t* q);

// 获取元素数量
u32 ring_queue_count(ring_queue_t* q);

// 清空队列
void ring_queue_clear(ring_queue_t* q);

#endif
