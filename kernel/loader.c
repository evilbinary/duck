/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "loader.h"

#include "fd.h"
#include "kernel.h"
#include "kernel/elf.h"
#include "thread.h"
#include "vfs.h"



loader_entry_fn load_thread_entry = (loader_entry_fn)run_load_thread;

void run_load_thread(void* arg) {
  log_debug("load not impl\n");
  syscall1(SYS_EXIT, -1);
}

void loader_regist(loader_entry_fn fn) { load_thread_entry = fn; }