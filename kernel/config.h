/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef CONFIG_H
#define CONFIG_H

#include "platform/platform.h"

#ifdef X86

#define MAX_FD_NUMBER 200
#define MAX_PHDR 16
#define MAX_SHDR 28

#define MAX_DEVICE 512

#define EXEC_FILE_NAME_LEN 64

#define MAX_MODULES 100

#define SYSCALL_NUMBER 600

#define KERNEL_THREAD_STACK_SIZE 1024*4  // 4k
#define THREAD_STACK_SIZE 1024*4        // 4k

#define PAGE_SIZE 0x1000
#define VM_ENABLE 1 //虚拟内存映射开启

#elif defined(ARMV7)

#define MAX_FD_NUMBER 50
#define MAX_PHDR 10
#define MAX_SHDR 20

#define MAX_DEVICE 20

#define EXEC_FILE_NAME_LEN 256

#define MAX_MODULES 10

#define SYSCALL_NUMBER 600

#define KERNEL_THREAD_STACK_SIZE 256
#define THREAD_STACK_SIZE 1024 * 2

#define PAGE_SIZE THREAD_STACK_SIZE

#define NO_THREAD_STACK0 1

#elif defined(GENERAL)

#define MAX_FD_NUMBER 64
#define MAX_PHDR 12
#define MAX_SHDR 25

#define MAX_DEVICE 40

#define EXEC_FILE_NAME_LEN 256

#define MAX_MODULES 40

#define SYSCALL_NUMBER 600

#define KERNEL_THREAD_STACK_SIZE 1024 * 4
#define THREAD_STACK_SIZE 1024*4  // 4k
#define PAGE_SIZE 0x1000

#elif defined(RISCV_VIRT)

#define MAX_FD_NUMBER 64
#define MAX_PHDR 12
#define MAX_SHDR 25

#define MAX_DEVICE 40

#define EXEC_FILE_NAME_LEN 256

#define MAX_MODULES 40

#define SYSCALL_NUMBER 600

#define KERNEL_THREAD_STACK_SIZE 1024 * 4
#define THREAD_STACK_SIZE 1024*4  // 4k
#define PAGE_SIZE 0x1000
#define VM_ENABLE 1 //虚拟内存映射开启
#elif defined(ESP32)

#define MAX_FD_NUMBER 64
#define MAX_PHDR 12
#define MAX_SHDR 25

#define MAX_DEVICE 40

#define EXEC_FILE_NAME_LEN 256

#define MAX_MODULES 20

#define SYSCALL_NUMBER 600

#define KERNEL_THREAD_STACK_SIZE 256
#define THREAD_STACK_SIZE 256  
#define PAGE_SIZE 256

#else

#define MAX_FD_NUMBER 128
#define MAX_PHDR 25
#define MAX_SHDR 25

#define MAX_DEVICE 40

#define EXEC_FILE_NAME_LEN 256

#define MAX_MODULES 40

#define SYSCALL_NUMBER 600

#define KERNEL_THREAD_STACK_SIZE 1024 * 4
#define THREAD_STACK_SIZE 1024*4  // 4k
#define PAGE_SIZE 0x1000
#define VM_ENABLE 1 //虚拟内存映射开启

#endif

#define SCHEDULE_FREQUENCY 1000

//是否打印加载ELF信息
// #define LOAD_ELF_DEBUG

//打印加载elf名字
// #define LOAD_ELF_NAME_DEBUG 1

//打印 sysfn信息
// #define LOG_DEBUG 0

#define MAX_PATH_BUFFER 256  //最长文件路径

#define kdbg kprintf("%s:%d\n", __FILE__, __LINE__)
#ifdef MALLOC_TRACE
#define fn_malloc kmalloc_trace
#define fn_free kfree_trace

#else
#define fn_malloc kmalloc
#define fn_free kfree
#endif
// #define fn_free kfree

#define LOG_COLOR 1
#define THREAD_DUMP_STOP_COUNT 1



#include "logger.h"

#endif