#include "ring_queue.h"
#include "kernel/memory.h"

ring_queue_t* ring_queue_create(u32 capacity, u32 elem_size) {
    ring_queue_t* q = kmalloc(sizeof(ring_queue_t), KERNEL_TYPE);
    if (q == NULL) return NULL;
    
    q->buffer = kmalloc(capacity * elem_size, KERNEL_TYPE);
    if (q->buffer == NULL) {
        kfree(q);
        return NULL;
    }
    
    q->capacity = capacity;
    q->elem_size = elem_size;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    
    return q;
}

void ring_queue_destroy(ring_queue_t* q) {
    if (q == NULL) return;
    if (q->buffer != NULL) {
        kfree(q->buffer);
    }
    kfree(q);
}

int ring_queue_put(ring_queue_t* q, void* elem) {
    if (q == NULL || elem == NULL) return 0;
    if (q->count >= q->capacity) return 0;  // 队列已满
    
    // 拷贝数据到 tail 位置
    u8* dst = q->buffer + (q->tail * q->elem_size);
    kmemcpy(dst, elem, q->elem_size);
    
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;
    
    return 1;
}

int ring_queue_poll(ring_queue_t* q, void* elem) {
    if (q == NULL || elem == NULL) return 0;
    if (q->count == 0) return 0;  // 队列为空
    
    // 从 head 位置拷贝数据
    u8* src = q->buffer + (q->head * q->elem_size);
    kmemcpy(elem, src, q->elem_size);
    
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    
    return 1;
}

int ring_queue_peek(ring_queue_t* q, void* elem) {
    if (q == NULL || elem == NULL) return 0;
    if (q->count == 0) return 0;
    
    u8* src = q->buffer + (q->head * q->elem_size);
    kmemcpy(elem, src, q->elem_size);
    
    return 1;
}

u32 ring_queue_is_empty(ring_queue_t* q) {
    if (q == NULL) return 1;
    return q->count == 0;
}

u32 ring_queue_is_full(ring_queue_t* q) {
    if (q == NULL) return 1;
    return q->count >= q->capacity;
}

u32 ring_queue_count(ring_queue_t* q) {
    if (q == NULL) return 0;
    return q->count;
}

void ring_queue_clear(ring_queue_t* q) {
    if (q == NULL) return;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
}
