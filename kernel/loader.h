/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef LOADER_H
#define LOADER_H


#include "config.h"

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