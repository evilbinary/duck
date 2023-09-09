/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef INIT_H
#define INIT_H
#include "kernel/kernel.h"

enum {
  SYS_EXIT = 1,
  SYS_FORK = 2,
  SYS_READ = 3,
  SYS_WRITE = 4,
  SYS_OPEN = 5,
  SYS_CLOSE = 6,
  SYS_UNLINK = 10,
  SYS_EXEC = 11,
  SYS_CHDIR = 12,
  SYS_SEEK = 19,
  SYS_GETPID = 20,
  SYS_ALARM = 27,
  SYS_ACESS = 33,
  SYS_KILL = 37,
  SYS_RENAME = 38,
  SYS_MKDIR = 39,
  SYS_PIPE = 42,
  SYS_DUP = 41,
  SYS_BRK = 45,
  SYS_SBRK = 46,  //?
  SYS_IOCTL = 54,
  SYS_UMASK = 60,
  SYS_DUP2 = 63,
  SYS_GETPPID = 64,
  SYS_READLINK = 85,
  SYS_READDIR = 89,
  SYS_MUNMAP = 91,
  SYS_STAT = 106,
  SYS_FSTAT = 108,
  SYS_SYSINFO = 116,
  SYS_CLONE = 120,
  SYS_MPROTECT = 125,
  SYS_FCHDIR = 133,
  SYS_LLSEEK = 140,
  SYS_GETDENTS = 141,
  SYS_READV = 145,
  SYS_WRITEV = 146,
  SYS_YIELD = 158,
  SYS_NANOSLEEP = 162,
  SYS_MREMAP = 163,
  SYS_RT_SIGACTION = 174,
  SYS_RT_SIGPROCMASK = 175,
  SYS_GETCWD = 183,
  SYS_MMAP2 = 192,
  SYS_MADVISE = 220,
  SYS_FCNT64 = 221,
  SYS_GETDENTS64 = 217,  // diff from x86
  SYS_CLOCK_NANOSLEEP = 230,
  SYS_FUTEX = 240,
  SYS_SET_THREAD_AREA = 243,
  SYS_EXIT_GROUP = 248,  // diff from x86
  SYS_SET_TID_ADDRESS = 256,
  SYS_STATX = 397,
  SYS_CLOCK_GETTIME64 = 403,
  SYS_PRINT = 500,
  SYS_PRINT_AT = 501,
  SYS_DEV_READ = 502,
  SYS_DEV_WRITE = 503,
  SYS_DEV_IOCTL = 504,
  SYS_TEST = 505,
  SYS_MAP = 506,
  SYS_UMAP = 507,
  SYS_VALLOC = 508,
  SYS_VFREE = 509,
  SYS_VHEAP = 510,
  SYS_DUMPS = 511,
  SYS_SELF = 512,
  SYS_MEMINFO = 513,
  SYS_THREAD_SELF = 514,
  SYS_THREAD_CREATE = 515,
  SYS_THREAD_DUMP = 516,
  SYS_THREAD_ADDR = 517,
};


#endif