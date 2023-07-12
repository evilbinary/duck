/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "kernel/kernel.h"

static void do_read_thread() {
  for (;;) {
    kprintf("test kernel thread\n");
  }
}

void test_kernel_thread() {
  thread_t* read_thread = thread_create_name_level(
      "poll", (u32*)&do_read_thread, NULL, LEVEL_KERNEL);
  thread_run(read_thread);
}

void test_vfs(){

  vnode_t* node=vfs_find(NULL,"/file/fgetc.txt");


}


int test_init(void) {
  log_info("test hello\n");
#ifdef X86
  test_ahci();
#endif
  // test_fat();
  
  // test_kernel_thread();

  // test_vfs();

  return 0;
}

void test_exit(void) { log_info("test exit\n"); }

module_t test_module = {.name = "test", .init = test_init, .exit = test_exit};
