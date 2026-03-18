// queue_pool.c - 兼容层实现
// 提供 queue_pool API，底层调用 ring_queue 实现
#include "queue_pool.h"

ring_queue_t* queue_pool_create(u32 capacity, u32 elem_size) {
    return ring_queue_create(capacity, elem_size);
}

ring_queue_t* queue_pool_create_align(u32 capacity, u32 elem_size, u32 align) {
    return ring_queue_create_align(capacity, elem_size, align);
}

int queue_pool_put(ring_queue_t* q, void* elem) {
    return ring_queue_put(q, elem);
}

int queue_pool_poll(ring_queue_t* q, void* elem) {
    return ring_queue_poll(q, elem);
}

void queue_pool_destroy(ring_queue_t* q) {
    ring_queue_destroy(q);
}

u32 queue_pool_count(ring_queue_t* q) {
    return ring_queue_count(q);
}

u32 queue_pool_is_empty(ring_queue_t* q) {
    return ring_queue_is_empty(q);
}

u32 queue_pool_is_full(ring_queue_t* q) {
    return ring_queue_is_full(q);
}

void queue_pool_clear(ring_queue_t* q) {
    ring_queue_clear(q);
}
