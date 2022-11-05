/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef THREAD_H
#define THREAD_H

#include "arch/arch.h"
#include "config.h"
#include "device.h"
#include "fd.h"
#include "memory.h"
#include "vfs.h"

#define THREAD_CREATE 0
#define THREAD_RUNNING 1
#define THREAD_RUNABLE 2
#define THREAD_STOPPED 3
#define THREAD_WAITING 4
#define THREAD_SLEEP 5
#define THREAD_UNINTERRUPTIBLE 15

#define PAGE_CLONE 1
#define PAGE_ALLOC 1 << 2
#define PAGE_SAME 1 << 3
#define PAGE_COPY_ON_WRITE 1 << 4  // copy on write
#define FS_CLONE 1 << 5
#define VM_CLONE_ALL 1 << 7
#define VM_CLONE 1 << 8
#define VM_SAME 1 << 9

#define STACK_CLONE 1 << 10
#define STACK_ALLOC 1 << 11
#define STACK_SAME 1 << 12

#define HEAP_CLONE 1 << 13
#define HEAP_ALLOC 1 << 14
#define HEAP_SAME 1 << 15

#define THREAD_DEFAULT (STACK_ALLOC|HEAP_ALLOC|PAGE_CLONE)

#define THREAD_FORK \
  (PAGE_CLONE | VM_CLONE_ALL | FS_CLONE | STACK_CLONE | HEAP_CLONE)
#define THREAD_CLONE (PAGE_CLONE | FS_CLONE)
#define THREAD_VFORK (PAGE_CLONE | VM_CLONE_ALL)

#ifndef KERNEL_THREAD_STACK_SIZE
#define KERNEL_THREAD_STACK_SIZE 1024 * 2
#endif

#ifndef THREAD_STACK_SIZE
#define THREAD_STACK_SIZE PAGE_SIZE
#endif
typedef struct thread {
  u32 id;
  context_t context;
  u8* name;
  int priority;
  int counter;
  int state;
  int sleep_counter;
  void* kstack;
  void* ustack;
  void* kstack_top;
  void* ustack_top;
  u32 ustack_size;
  u32 kstack_size;
  struct thread_s* next;
  void* data;
  void* exec;
  vmemory_area_t* vmm;
  u32 pid;
  u32** fds;
  u32 fd_size;
  u32 fd_number;
  u32 lock;
  u32 code;
  u32 fault_count;
  vfs_t* vfs;
  u32 level;
  u32* entry;
  u32 cpu_id;
} thread_t;

void thread_init();

thread_t* thread_create(void* entry, void* data);
thread_t* thread_create_name(char* name, void* entry, void* data);

thread_t* thread_create_name_level(char* name, void* entry, void* data,
                                   u32 level);

thread_t* thread_create_ex(void* entry, u32 kstack_size, u32 size, void* data,
                           u32 level, u32 flags);

thread_t* thread_create_ex_name(char* name, void* entry, u32 size, void* data,
                                u32 level, u32 flags);
int thread_init_vm(thread_t* copy, thread_t* thread, u32 flags);

void thread_sleep(thread_t* thread, u32 count);

void thread_wake(thread_t* thread);

void thread_add(thread_t* thread);

void thread_remove(thread_t* thread);

void thread_run(thread_t* thread);
void thread_stop(thread_t* thread);
void thread_exit(thread_t* thread, int code);

void thread_yield();
thread_t* thread_current();
thread_t* thread_copy(thread_t* thread, u32 flags);
int thread_add_fd(thread_t* thread, fd_t* fd);
fd_t* thread_find_fd_id(thread_t* thread, u32 fd);
int thread_find_fd_name(thread_t* thread, u8* name);
fd_t* thread_set_fd(thread_t* thread, u32 fd, fd_t* nfd);

thread_t* thread_find_next(thread_t* thread);

thread_t* thread_head();

void thread_run_all();
void thread_reset_stack3(thread_t* thread, u32* stack3);

void thread_fill_fd(thread_t* thread);

#endif