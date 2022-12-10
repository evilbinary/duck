/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/

#include "kernel.h"

void kernel_init() {
  int cpu=cpu_get_id();
  if(cpu==0){
    log_info("kernel init\n");
    log_info("log init\n");
    log_init();
    log_info("exception init\n");
    exception_init();
    log_info("page init\n");
    page_init();
    log_info("syscall init\n");
    syscall_init();
    log_info("schedule init\n");
    schedule_init();
    log_info("module init\n");
    module_init();
    log_info("memory init\n");
    memory_init();
    log_info("vfs init\n");
    vfs_init();
    log_info("kernel init end\n");
    thread_init();
  }else{
    log_info("ap kernel init\n");
    schedule_init();
  }
}

void kernel_run(){
  context_t* context=thread_current_context();
  context_restore(context);
}
