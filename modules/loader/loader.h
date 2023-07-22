/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef __LOAER_H__
#define __LOAER_H__

#ifndef MAX_PHDR
#define MAX_PHDR 12
#endif

#ifndef MAX_SHDR
#define MAX_SHDR 30
#endif


typedef int (*load_fn)(void* data, u32 fd);
typedef int (*check_type_fn)(void* data, u32 fd);


typedef struct load{
    int type;
    check_type_fn check_type;
    load_fn fn;
}load_t;

typedef struct exec{
    int argc;
    char ** argv;
    char **envp;
    char* filename;
}exec_t;

typedef int (*entry_fn)(exec_t*);


void run_elf_thread(void* arg);


#endif