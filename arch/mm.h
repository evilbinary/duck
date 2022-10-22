/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef ARCH_MM_H
#define ARCH_MM_H

#include "boot.h"
#include "libs/include/types.h"

#ifdef ARM
#ifdef ARMV7_A
#include "armv7-a/mm.h"
#elif defined(ARMV7)
#include "armv7/mm.h"
#elif defined(ARMV5)
#include "armv5/mm.h"
#elif defined(ARMV8_A)
#include "armv8-a/mm.h"
#else
#error "no support arm"
#endif
#elif defined(X86)
#include "x86/mm.h"
#elif defined(LX6)
#include "lx6/mm.h"
#else
#error "no support"
#endif

#define NR_BLOCKS 30
#define MEM_FREE 1
#define MEM_USED 0x10
#define MEM_LOCK 3
#define MEM_SHARE 4
#define MEM_HEAD 5

#define PAGE_SIZE 0x1000

typedef struct mem_block {
  u32 addr;
  u32 type;
  u32 size;
  u32 origin_size;
  u32 origin_addr;
  struct mem_block* next;
} __attribute__((packed)) mem_block_t;

typedef struct block {
  size_t size;
  struct block* next;
  struct block* prev;
  u32 free;
  u32 magic;
} block_t;

typedef void* (*mm_alloc_fn)(size_t size);
typedef void (*mm_free_fn)(void* ptr);
typedef void (*mm_init_fn)();
typedef struct memory_manager {
  mm_alloc_fn alloc;
  mm_free_fn free;
  mm_init_fn init;
  mem_block_t* blocks;
  mem_block_t* blocks_tail;

  block_t* g_block_list;
  block_t* g_block_list_last;
} memory_manager_t;

u32* page_alloc_clone(u32* page_dir, u32 level);
void page_clone(u32* old_page, u32* new_page);

#ifdef MALLOC_TRACE
#define kmalloc(size) kmalloc_trace(size, __FILE__, __LINE__, __FUNCTION__)
#define kmalloc_alignment(size, alignment) \
  kmalloc_alignment_trace(size, alignment, __FILE__, __LINE__, __FUNCTION__)
#define kfree(ptr) kfree_trace(ptr, __FILE__, __LINE__, __FUNCTION__)
#define kfree_alignment(ptr) \
  kfree_alignment_trace(ptr, __FILE__, __LINE__, __FUNCTION__)

#else
void* kmalloc(size_t size);
void* kmalloc_alignment(size_t size, int alignment);

void kfree(void* ptr);
void kfree_alignment(void* ptr);
#endif

void mm_init();
void* mm_alloc(size_t size);
void mm_free(void* p);
void* mm_alloc_zero_align(size_t size, u32 alignment);
void mm_alloc_init();
void mm_dump_phy();
void mm_dump();

#endif