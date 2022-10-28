/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef LOADER_H
#define LOADER_H


#include "config.h"

#ifdef X86 
#define EXEC_ADDR  0x200000
#define STACK_ADDR 0x80000000
#define HEAP_ADDR  0x82000000

#define KERNEL_OFFSET 0x10000000

#elif defined(ARM)
#define EXEC_ADDR  0x71000000
#define STACK_ADDR 0x70000000
#define HEAP_ADDR  0x70100000
#else
#define EXEC_ADDR  0x71000000
#define STACK_ADDR 0x70000000
#define HEAP_ADDR  0x70100000
#endif

#define KEXEC_ADDR  EXEC_ADDR + KERNEL_OFFSET
#define KSTACK_ADDR STACK_ADDR + KERNEL_OFFSET
#define KHEAP_ADDR  HEAP_ADDR + KERNEL_OFFSET

#ifndef MAX_PHDR 
#define MAX_PHDR 12
#endif

#ifndef MAX_SHDR 
#define MAX_SHDR 30
#endif

typedef struct exec{
    int argc;
    char** argv;
    char **envp;
    char* filename;
}exec_t;

typedef int (*entry_fn)(exec_t*);

void run_elf_thread();

#endif