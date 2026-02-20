/*******************************************************************
 * Copyright 2021-present evilbinary
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

#define LEVEL_KERNEL 0
#define LEVEL_KERNEL_SHARE 1
#define LEVEL_USER 3


#define FS_CLONE 1 << 1
#define VM_CLONE_ALL 1 << 2
#define VM_CLONE 1 << 3
#define VM_ALLOC 1 << 4
#define VM_SAME 1 << 5
#define VM_COW 1 << 6  // copy on write

#define THREAD_NEW (VM_CLONE)
#define THREAD_FORK (VM_CLONE_ALL | FS_CLONE)
#define THREAD_CLONE (VM_CLONE_ALL | FS_CLONE)
#define THREAD_VFORK (VM_CLONE_ALL)

#define DUMP_DEFAULT 1
#define DUMP_CONTEXT 2
#define DUMP_STACK 4

#ifndef THREAD_DUMP_STOP_COUNT
#define THREAD_DUMP_STOP_COUNT 2
#endif



typedef struct thread {
  u32 id;
  u8* name;
  int priority;
  int counter;
  u32 ticks;
  int state;
  int sleep_counter;
  struct thread* next;
  void* data;
  void* exec;
  context_t* ctx;
  vmemory_t* vm;
  u32 pid;
  fd_t** fds;
  u32 fd_size;
  u32 fd_number;
  u32 lock;
  u32 code;
  u32 fault_count;
  u32 faults;
  vfs_t* vfs;
  u32 level;
  u32 cpu_id;
  u32 mem;
  u32 dump_count;
  void* tinfo;
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
context_t* thread_current_context();
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
void thread_dump_stack(u32* stack, u32 size);
void thread_dump(thread_t* thread, u32 flags);

#endif