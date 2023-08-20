/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "init.h"

#define VERSION "1.3"

const char* logo =
    " _  _  _ _  _  _      \n"
    "| || || | || || |     \n"
    "| \\| |/ | \\| |/ __  __\n"
    " \\_   _/ \\_   _/  \\/ /\n"
    "   | |     | |( ()  < \n"
    "   |_|     |_| \\__/\\_\\\n\n"
    "2021 - 2080 Copyright by evilbinary \n";

const char* build_str = "version " VERSION " " __DATE__ " " __TIME__
                        "\n"
                        "https://github.com/evilbinary/YiYiYa\n";

const char* welcome = "\nWelcome to YiYiYa Os ^_^! \n\n";

void print_string(char* str) { syscall1(SYS_PRINT, str); }

void print_logo() {
  print_string(logo);
  print_string(build_str);
  print_string(welcome);
}

void print_promot() { print_string("yiyiya$"); }

void print_help() { print_string("supports help ps cd pwd command\n"); }

void ps_command() { syscall0(SYS_DUMPS); }

void mem_info_command() { syscall0(SYS_MEMINFO); }

void pre_launch();
void build_env(char** env);

extern int module_ready;

#define USE_FORK 1

void hello_thread(void) {
  print_string("hello\n");
  char env_buf[512];
  char** env = env_buf;
  build_env(env);
  char* cmd = "/ls";
  syscall3(SYS_EXEC, cmd, NULL, env);
  syscall1(SYS_EXIT, 0);
}

char* argv_p[64];
char env_p[512];

void reopen(char* name) {
  int series = syscall2(SYS_OPEN, name, 0);
  if (series <= 0) {
    print_string("error open series\n");
  }

  if (syscall2(SYS_DUP2, series, 1) < 0) {
    print_string("err in dup2\n");
  }
  if (syscall2(SYS_DUP2, series, 0) < 0) {
    print_string("err in dup2\n");
  }
  if (syscall2(SYS_DUP2, series, 2) < 0) {
    print_string("err in dup2\n");
  }
}

int run_exec(char* cmd, char** argv, char** env) {
#ifdef USE_FORK
  char temp[128];  // 0xffffffb8 addr fix me
  int pid = syscall0(SYS_FORK);
  int p = syscall0(SYS_GETPID);
  if (pid == 0) {  // 子进程
    // reopen( "/dev/log");
    syscall3(SYS_EXEC, cmd, argv, env);
    syscall1(SYS_EXIT, 0);
  }
#else
  thread_t* t = syscall3(SYS_THREAD_CREATE, cmd, (u32*)&hello_thread, NULL);
#endif
}

int do_exec(char* cmd, int count, char** env) {
  char buf[64];
  cmd[count] = 0;
  char* argv[64];
  int i = 0;
  const char* split = " ";
  char* ptr = kstrtok(cmd, split);
  while (ptr != NULL) {
    argv_p[i] = ptr;
    argv[i++] = ptr;
    ptr = kstrtok(NULL, split);
  }
  if (i <= 0 || argv[1] == ' ' || argv[0] == NULL) {
    return 0;
  }
  sprintf(buf, "/bin/%s", argv[0]);
  int ret = syscall2(SYS_ACESS, buf, 0);
  if (ret < 0) {
    sprintf(buf, "/%s", argv[0]);
    ret = syscall2(SYS_ACESS, buf, 0);
  }
  int pid = 0;
  if (ret == 0) {
    pid = run_exec(buf, argv, env);
  }
  return pid;
}

void cd_command(char* cmd, int count) {
  char buf[64];
  cmd[count] = 0;
  char* argv[64];
  int i = 0;
  const char* split = " ";
  char* ptr = kstrtok(cmd, split);
  ptr = kstrtok(NULL, split);
  print_string(ptr);
  syscall1(SYS_CHDIR, ptr);
}

void pwd_command() {
  char buf[128];
  kmemset(buf, 0, 128);
  syscall2(SYS_GETCWD, buf, 128);
  print_string(buf);
  print_string("\n");
}

void do_shell_cmd(char* cmd, int count, char** env) {
  if (count == 0) return;
  if (kstrncmp(cmd, "help", count) == 0) {
    print_help();
  } else if (kstrncmp(cmd, "cd", 2) == 0) {
    cd_command(cmd, count);
  } else if (kstrncmp(cmd, "pwd", 3) == 0) {
    pwd_command();
  } else if (kstrncmp(cmd, "ps", 2) == 0) {
    ps_command();
  } else if (kstrncmp(cmd, "mem", 3) == 0) {
    mem_info_command();
  } else {
    int ret = do_exec(cmd, count, env);
    if (ret < 0) {
      print_string(cmd);
      print_string(" command not support\n");
    }
  }
  kmemset(cmd, 0, count);
}

void sleep() {
  u32 tv[2] = {5, 0};
  syscall4(SYS_CLOCK_NANOSLEEP, 0, 0, &tv, &tv);
}

void do_init_thread(void) {
  char buf[64];
  int count = 0;
  int ret = 0;

  // wait module ready
  while (module_ready <= 0) {
    // sleep();
  }

  print_logo();
  print_promot();

  int series = syscall2(SYS_OPEN, "/dev/series", 0);
  if (series <= 0) {
    print_string("error open series\n");
  }

  if (syscall2(SYS_DUP2, series, 1) < 0) {
    print_string("err in dup2\n");
  }
  if (syscall2(SYS_DUP2, series, 0) < 0) {
    print_string("err in dup2\n");
  }
  if (syscall2(SYS_DUP2, series, 2) < 0) {
    print_string("err in dup2\n");
  }

  syscall1(SYS_CHDIR, "/");

  char** env = env_p;
  build_env(env);
  pre_launch();

  for (;;) {
    int ch = 0;
    ret = syscall3(SYS_READ, 0, &ch, 1);
    if (ret > 0) {
      if (ch == '\r' || ch == '\n') {
        print_string("\n");
        do_shell_cmd(buf, count, env);
        count = 0;
        print_promot();
      } else if (ch == 127) {
        if (count > 0) {
          print_string("\n");
          buf[--count] = 0;
          print_promot();
          print_string(buf);
        }
      } else {
        buf[count++] = ch;
        syscall3(SYS_WRITE, 1, &ch, 1);
      }
    } else {
      sleep();
    }
  }
}

void try_run(char* cmd, char** argv, char** env) {
  char buf[64];
  sprintf(buf, "/bin/%s", argv[0]);
  int ret = syscall2(SYS_ACESS, buf, 0);
  if (ret < 0) {
    sprintf(buf, "/%s", argv[0]);
    ret = syscall2(SYS_ACESS, buf, 0);
  }
  if (ret >= 0) {
    run_exec(buf, argv, env);
    for (;;) {
      sleep();
    }
  }
}


void pre_launch() {
  // must init global for armv7-a
  char* scm_argv[] = {"/scheme", "-b", "/scheme.boot", "--verbose", NULL};
  char* lua_argv[] = {"/lua", "/hello.lua", NULL};
  char* mgba_argv[] = {"mgba", "/mario.gba", NULL};
  char* cat_argv[] = {"/cat", "hello.lua", NULL};
  char* showimg_argv[] = {"/showimage", "/pngtest.png", NULL};
  char* gnuboy_argv[] = {"/gnuboy", "./pokemon.gbc", NULL};
  char* nes_argv[] = {"infones", "/mario.nes", NULL};
  char* shell_argv[] = {"shell", NULL, NULL};

#ifdef X86
  // int fd = syscall2(SYS_OPEN, "/dev/stdin", 0);
  // syscall3(SYS_EXEC,"/ls",NULL,NULL);
  // syscall3(SYS_EXEC,"/gui",NULL,NULL);
  // syscall3(SYS_EXEC,"/test-file",NULL,NULL);
  // syscall3(SYS_EXEC,"/test-mem",NULL,NULL);
  // syscall3(SYS_EXEC,"/test-uncompress",NULL,NULL);
  // syscall3(SYS_EXEC,"/test-string",NULL,NULL);
  // syscall3(SYS_EXEC,"/test-stdlib",NULL,NULL);
  // syscall3(SYS_EXEC,"/test-stdio",NULL,NULL);

  // syscall3(SYS_EXEC, "/luat", NULL);

  // syscall3(SYS_EXEC, "/etk", NULL);
  // syscall3(SYS_EXEC,"/test-rs",NULL,NULL);
  // syscall3(SYS_EXEC, "/lua", lua_argv,NULL);
  // syscall3(SYS_EXEC,"/launcher",NULL,NULL);
  // syscall3(SYS_EXEC,"/track",NULL,NULL);
  // syscall3(SYS_EXEC,"/test",NULL,NULL);
  // syscall3(SYS_EXEC,"/microui",NULL,NULL);
  // syscall3(SYS_EXEC,"/lvgl",NULL,NULL);
  // kprintf("fd=>%d\n",fd);

  // syscall3(SYS_EXEC, "/infones", nes_argv,NULL);
  // syscall3(SYS_EXEC, "/mgba", mgba_argv,NULL);
  // syscall3(SYS_EXEC, "/scheme", scm_argv,NULL);
  // syscall3(SYS_EXEC, "/sdl2", NULL,NULL);
  // syscall3(SYS_EXEC, "/showimage", showimg_argv,NULL);
  // syscall3(SYS_EXEC, "/gnuboy", gnuboy_argv,NULL);

  // for (;;)
  //   ;
#elif defined(ARMV7_A)
//  try_run("/bin/shell", shell_argv, env_p);
#elif defined(ARMV7)
  // test_lcd();
#else defined(ARM)
  // syscall3(SYS_EXEC,"/hello-rs",NULL,NULL);
  // syscall3(SYS_EXEC,"/test-rs",NULL,NULL);
  // syscall3(SYS_EXEC,"/ls",NULL,NULL);
  // syscall3(SYS_EXEC, "/test", NULL);
  // syscall3(SYS_EXEC,"/hello",NULL,NULL);
  // syscall3(SYS_EXEC, "/lvgl", NULL);
  // syscall3(SYS_EXEC, "/launcher", NULL);

  // syscall3(SYS_EXEC,"/track",NULL,NULL);
  // syscall3(SYS_EXEC,"/gui",NULL,NULL);
  // syscall3(SYS_EXEC,"/etk",NULL,NULL);
  //  syscall3(SYS_EXEC,"/test",NULL,NULL);
  //  syscall3(SYS_EXEC,"/microui",NULL,NULL);

  // syscall3(SYS_EXEC, "/lua", lua_argv,NULL);

  // syscall3(SYS_EXEC,"/test-musl",NULL,NULL);
  // syscall3(SYS_EXEC, "/scheme", scm_argv,NULL);
  // syscall3(SYS_EXEC, "/sdl2", NULL,NULL);
  // syscall3(SYS_EXEC, "/mgba", mgba_argv,NULL);
  // syscall3(SYS_EXEC, "/player", mgba_argv,NULL);
  // syscall3(SYS_EXEC, "/cat", cat_argv,NULL);
  // syscall3(SYS_EXEC, "/infones", nes_argv,NULL);
  // syscall3(SYS_EXEC,"/test-file",NULL,NULL);
  // syscall3(SYS_EXEC, "/gnuboy", gnuboy_argv,NULL);
  // syscall3(SYS_EXEC, "/showimage", showimg_argv,NULL);

// test_cpu_speed();
//  for(;;);
#endif
}

void auxv_set(Elf32_auxv_t* auxv, int a_type, int a_val) {
  auxv[a_type].a_type = a_type;
  auxv[a_type].a_un.a_val = a_val;
}

void build_env(char** env) {
  char** envp = env;
  Elf32_auxv_t* auxv = envp;

  auxv_set(auxv++, AT_IGNORE, 0);
  auxv_set(auxv++, AT_EXECFD, 0);
  auxv_set(auxv++, AT_PHDR, 0);
  auxv_set(auxv++, AT_PHENT, 0);
  auxv_set(auxv++, AT_PHNUM, 0);
  auxv_set(auxv++, AT_PAGESZ, PAGE_SIZE);
  auxv_set(auxv++, AT_BASE, 0);
  auxv_set(auxv++, AT_FLAGS, 0);
  auxv_set(auxv++, AT_ENTRY, 0);
  auxv_set(auxv++, AT_NOTELF, 0);
  auxv_set(auxv++, AT_UID, 0);
  auxv_set(auxv++, AT_EUID, 0);
  auxv_set(auxv++, AT_GID, 0);
  auxv_set(auxv++, AT_EGID, 0);
  auxv_set(auxv++, AT_PLATFORM, 0);
  auxv_set(auxv++, AT_HWCAP, 0);
  auxv_set(auxv++, AT_CLKTCK, 0);
  auxv_set(auxv++, AT_NULL, 0);
}
