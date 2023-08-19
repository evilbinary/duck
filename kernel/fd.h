/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef FD_H
#define FD_H

#include "config.h"

#define STDIN 0
#define STDOUT 1
#define STDERR 2
#define STDSELF 3

#include "types.h"

#define	O_CREAT			0100
#define	O_EXCL			0200
#define	O_NOCTTY		0400
#define	O_TRUNC			01000

#ifndef MAX_FD_NUMBER
#define MAX_FD_NUMBER 512
#endif

typedef struct fd {
  u32 id;
  u32 type;  // file ,socket, pipe dir
  u32 *data;
  u32 offset;
  u8 *name;
  u32 use_count;
} fd_t;


fd_t* fd_open(u32* file, u32 type, char* name);
fd_t* fd_find(u32 fd);
int fd_close(fd_t* fd);
int fd_init();
int fd_std_init();

#endif