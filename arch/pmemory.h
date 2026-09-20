/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef ARCH_MEMORY_H
#define ARCH_MEMORY_H

#include "boot.h"
#include "libs/include/types.h"

/* 【架构 mm.h 的选择】优先用构建宏（构建系统显式指定的目标架构，权威），
 * 再以【编译器内建宏】兜底：万一某个编译单元拿不到构建宏（duck/arch/ 下的
 * 通用源在部分构建方式下会这样），也不会掉进 general/mm.h 让 PAGE_* 全变 0、
 * 让 page_map_on 原型不可见（那会导致 flags 参数丢失 ⇒ 整片 DRAM 被映成
 * Strongly-ordered，实机 238~271 周期/字节、渲染 7fps）。
 * 注意：不能只用内建宏 —— 例如 dmulator 目标是 general 架构但用 -m32 编译，
 * 内建宏是 __i386__，会误选到 x86/mm.h。 */
#if defined(ARMV7_A)
#include "armv7-a/mm.h"
#elif defined(ARMV7)
#include "armv7/mm.h"
#elif defined(ARMV5)
#include "armv5/mm.h"
#elif defined(ARMV8_A)
#include "armv8-a/mm.h"
#elif defined(X86)
#include "x86/mm.h"
#elif defined(LX6)
#include "lx6/mm.h"
#elif defined(RISCV)
#include "riscv/mm.h"
#elif defined(DUMMY)
#include "dummy/mm.h"
#elif defined(GENERAL)
#include "general/mm.h"
/* 以下为编译器内建宏兜底（构建宏缺失时启用） */
#elif defined(__ARM_ARCH_7A__)
#include "armv7-a/mm.h"
#elif defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_7M__) || \
    defined(__ARM_ARCH_7__)
#include "armv7/mm.h"
#elif defined(__ARM_ARCH_5TE__) || defined(__ARM_ARCH_5T__) || \
    defined(__ARM_ARCH_5__) || defined(__ARM_ARCH_4T__) || defined(__ARM_ARCH_4__)
#include "armv5/mm.h"
#elif defined(__aarch64__)
#include "armv8-a/mm.h"
#elif defined(__riscv)
#include "riscv/mm.h"
#elif defined(__XTENSA__)
#include "lx6/mm.h"
#else
#include "general/mm.h"
#endif

/* ---- 诊断总线开关 ---------------------------------------------------------
 * 默认关闭（0）：页属性守卫（MAP_NC_RAM）、内存带宽/属性自测（MEMBW/MEMRW/
 * MEMATTR）等探针都由它包裹，避免刷屏、也不拖慢启动。
 * 复查页属性时构建加 -DMM_DEBUG_PROBE=1 即可全部打开。 */
#ifndef MM_DEBUG_PROBE
#define MM_DEBUG_PROBE 0
#endif

/* 【页映射 API 原型】必须让所有调用 TU 都看得见。
 * 缺原型 ⇒ 隐式声明 ⇒ 参数寄存器配对错位（u64 的 flags 会被放到别的寄存器对，
 * 被调用方读到的 r3 就不是属性值）⇒ 属性恒为 0、整片内存被映成
 * Strongly-ordered（实机 238~271 周期/字节、渲染 7fps）。
 * page_map / page_map_current 的声明在 kernel/page.h，page_map_on 在各架构 mm.h。 */
void mm_parse_map(void* kernel_page_dir);
void map_mem_block(void* page, vaddr_t max_size, u64 flags);
void page_map_kernel(void* page, u64 flag_x, u64 flag_rw);

#define NR_BLOCKS 30
#define MEM_FREE 1
#define MEM_USED 0x10
#define MEM_LOCK 3
#define MEM_SHARE 4
#define MEM_HEAD 5

#ifndef PAGE_SIZE
#define PAGE_SIZE 0x1000
#endif

typedef struct mem_block {
  uintptr_t addr;
  u32 type;
  size_t size;
  size_t origin_size;
  uintptr_t origin_addr;
  struct mem_block* next;
} __attribute__((packed)) mem_block_t;

typedef struct block {
  size_t size;
  struct block* next;
  struct block* prev;
  u32 free;
  u32 count;
  u32 no;
  int tid;
  u32 magic;
} block_t;

typedef void* (*mm_alloc_fn)(size_t size);
typedef void (*mm_free_fn)(void* ptr);
typedef void (*mm_init_fn)();
typedef size_t (*mm_size_fn)(void* ptr);

typedef struct memory_manager {
  mm_alloc_fn alloc;
  mm_free_fn free;
  mm_init_fn init;
  mm_size_fn size;
  mem_block_t* blocks;
  mem_block_t* blocks_tail;

  block_t* g_block_list;
  block_t* g_block_list_last;
  block_t* g_block_free;
  block_t* g_block_free_last;
  
  u32 alloc_count;
  u32 alloc_size;
  uintptr_t last_map_addr;
  u32 extend_phy_count;
} memory_manager_t;

#if defined(ARM64)
u64* page_create(u32 level);
/* 归还整棵用户页表：用户物理页回页分配器，各级表本身回内核堆。
 * 架构相关（表级数/项宽/掩码不同），由各 arch 的 mm.c 实现。
 * 只处理用户区（>= EXEC_ADDR）；内核共享区绝不能释放。 */
void page_destroy(u64* upage);
#else
u32* page_create(u32 level);
/* 同上，32 位页表版本（armv5/armv7/armv7-a 等） */
void page_destroy(u32* upage);
#endif

#ifdef MALLOC_TRACE
#define kmalloc(size) kmalloc_trace(size, __FILE__, __LINE__, __FUNCTION__)
#define kmalloc_alignment(size, alignment) \
  kmalloc_alignment_trace(size, alignment, __FILE__, __LINE__, __FUNCTION__)
#define kfree(ptr) kfree_trace(ptr, __FILE__, __LINE__, __FUNCTION__)
#define kfree_alignment(ptr) \
  kfree_alignment_trace(ptr, __FILE__, __LINE__, __FUNCTION__)

#else

#endif

void mm_init();
void* mm_alloc(size_t size);
void mm_free(void* p);
void* mm_alloc_zero_align(size_t size, u32 alignment);
void* mm_alloc_page(void);
void mm_free_page(void* p);
/* p 是否为 boot_info 声明过的 RAM（type==1）内的页对齐地址。
 * 用途：页池的空闲链 next 指针就存在被释放页自身头部，链上一旦出现非法值，
 * mm_alloc_page 会把任意地址当页发出去（cpu_zero_phy_page 写它即总线外部
 * 中止）；释放侧（page_destroy/vfree）也用它在归还前筛掉设备/未认领页。 */
/* 位置合法性：p 是否落在 boot_info 声明的 RAM（type==1）内。
 * 无关"归谁"：设备（显存）段已在 boot 侧从 RAM 条目里排除，所以它天然不在其中。 */
int mm_page_in_ram(void* p);
void mm_alloc_init();
void mm_dump_phy();
void mm_dump();

void page_map_range(void* page, vaddr_t vaddr, vaddr_t paddr, vaddr_t size, u64 flag);

#endif