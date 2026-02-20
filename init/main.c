/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "main.h"

extern void modules_init();
extern void do_init_thread(void);
extern void do_kernel_thread();
extern void do_monitor_thread();

void kstart(int argc, char* argv[], char** envp) {
  boot_info_t* boot_info = envp[0];
  // int cpu = envp[1];
  int cpu = cpu_get_id();
  arch_init(boot_info, cpu);
  if (cpu == 0) {
    kmain(argc, argv);
  } else {
    ksecondary(cpu, argc, argv);
  }
  for (;;) {
    cpu_halt();
  }
}

int kmain(int argc, char* argv[]) {
  kernel_init();

  log_info("kernel thread init\n");

  thread_t* t1 = thread_create_name_level("kernel", (void*)&do_kernel_thread,
                                          NULL, LEVEL_KERNEL_SHARE);
  thread_t* t2 = thread_create_name("init", (void*)&do_init_thread, NULL);
  thread_run(t1);
  thread_run(t2);

  log_info("kernel run start\n");

  kernel_run();

  return 0;
}

int ksecondary(int cpu, int argc, char* argv) {
  kernel_init();
  // will start after main start
  thread_t* t1 = thread_create_name("monitor", (void*)&do_monitor_thread, NULL);
  thread_run(t1);

  thread_t* t2 = thread_create_name("monitor2", (void*)&do_monitor_thread, NULL);
  thread_run(t2);

  log_info("kernel run secondary %d\n", cpu);
  kernel_run();

  return 0;
}
