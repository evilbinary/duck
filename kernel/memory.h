/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef MEMORY_H
#define MEMORY_H

#include "arch/arch.h"
#include "kernel/config.h"

#ifdef X86
#define EXEC_ADDR 0x40000000  // 1gb exec space to stack
#define STACK_ADDR 0x80000000
#define HEAP_ADDR 0x82000000
#define KERNEL_OFFSET 0x10000000

#elif defined(ARM)
#define KERNEL_OFFSET 0x20000000 //
#define EXEC_ADDR  0x60000000   // 0x60000000-0x66400000 0xc0000000
#define STACK_ADDR 0x70000000   // 0x70000000-0x70100000 0xd0000000
#define HEAP_ADDR  0x70100000   // 0x70100000-0x76500000 0xd0000000
#else
#define KERNEL_OFFSET 0x10000000
#define EXEC_ADDR  0x60000000  // 0x60000000-0x66400000
#define STACK_ADDR 0x70000000 // 0x70000000-0x70100000
#define HEAP_ADDR  0x70100000  // 0x70100000-0x76500000
#endif

#define MEMORY_FREE 0
#define MEMORY_USE 1
#define MEMORY_SHARE 2
#define MEMORY_HEAP 3
#define MEMORY_EXEC 4
#define MEMORY_DATA 5
#define MEMORY_STACK 6
#define MEMORY_MMAP 7

#define MEMORY_STACK_SIZE 1024 * 1024   // 1m
#define MEMORY_HEAP_SIZE 1024 * 1024 * 100  // 100m
#define MEMORY_EXEC_SIZE 1024 * 1024 * 100  // 100m

#define KERNEL_POOL_NUM 20
#define USER_POOL_NUM 20

#define MEMORY_TYPE_USE 1   // 使用
#define MEMORY_TYPE_FREE 2  // 释放

#define MEMORY_ALIGMENT 16

#define DEFAULT_TYPE 1 << 0
#define KERNEL_TYPE 1 << 1
#define DEVICE_TYPE 1 << 2

typedef struct memory {
  ulong total;
  ulong free;
  ulong kernel_used;
  ulong user_used;
} memory_t;

typedef struct vmemory_area {
  u32 vaddr;
  u32 vend;
  u32 size;
  u8 flags;
  u32 alloc_addr;
  u32 alloc_size;
  struct vmemory_area* next;
  struct vmemory_area* child;
} vmemory_area_t;

#define ALIGN(x, a) (x + (a - 1)) & ~(a - 1)

#ifdef MALLOC_TRACE
#define kmalloc(size, flag) \
  kmalloc_trace(size, flag, __FILE__, __LINE__, __FUNCTION__)
#define kmalloc_alignment(size, alignment, flag)                     \
  kmalloc_alignment_trace(size, alignment, flag, __FILE__, __LINE__, \
                          __FUNCTION__)
#define kfree(ptr) kfree_trace(ptr, __FILE__, __LINE__, __FUNCTION__)
#define kfree_alignment(ptr) \
  kfree_alignment_trace(ptr, __FILE__, __LINE__, __FUNCTION__)

#else
void* kmalloc(size_t size, u32 flag);
void* kmalloc_alignment(size_t size, int alignment, u32 flag);

void kfree(void* ptr);
void kfree_alignment(void* ptr);
#endif

void* valloc(void* addr, size_t size);
void vfree(void* addr, size_t size);

void kpool_init();
void* kpool_poll();
int kpool_put(void* e);

void use_kernel_page();
void use_user_page();

void* page_v2p(u64* page_dir_ptr_tab, void* vaddr);
void* kpage_v2p(void* addr, int size);

void memory_init();

vmemory_area_t* vmemory_area_create(void* addr, u32 size, u8 flags);
vmemory_area_t* vmemory_area_destroy(vmemory_area_t* area);

vmemory_area_t* vmemory_area_alloc(vmemory_area_t* areas, void* addr, u32 size);
vmemory_area_t* vmemory_area_find(vmemory_area_t* areas, void* addr,
                                  size_t size);
void vmemory_area_add(vmemory_area_t* areas, vmemory_area_t* area);
void vmemory_area_free(vmemory_area_t* area);

#endif