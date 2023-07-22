/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef LOADER_H
#define LOADER_H

#include "config.h"

typedef void (*loader_entry_fn)(void* arg);

void loader_regist(loader_entry_fn fn);


extern loader_entry_fn load_thread_entry;

void run_load_thread(void* arg);

#endif