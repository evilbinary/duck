
/*******************************************************************
* Copyright 2021-2080 evilbinary
* 作者: evilbinary on 01/01/20
* 邮箱: rootdebug@163.com
********************************************************************/
#ifndef MODULE_H
#define MODULE_H

#include "arch/arch.h"
#include "memory.h"
#include "config.h"

#ifndef MAX_MODULES
#define MAX_MODULES 256
#endif

typedef void (*mod_init_fn)();
typedef void (*mod_exit_fn)();


typedef struct module{
    char* name;
    mod_init_fn init;
    mod_exit_fn exit;
} module_t;



void module_init();

#ifdef MODULE_STATIC 
#define module_regist(mod) modules[module_number++]=mod;
#else
void module_regist(module_t* mod);
#endif
void module_unregist(module_t* mod);

#endif