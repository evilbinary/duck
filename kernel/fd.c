/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/

#include "fd.h"

#include "device.h"
#include "vfs.h"

fd_t fd_list[MAX_FD_NUMBER];
int fd_number = 0;

fd_t* fd_find(u32 fd) {
  for (int i = 0; i < fd_number; i++) {
    if (fd_list[i].id == fd) {
      return &fd_list[i];
    }
  }
  return NULL;
}

fd_t* fd_open(u32* file, u32 type, char* name) {
  if (fd_number > MAX_FD_NUMBER) {
    kprintf("new fd limit \n");
    return NULL;
  }
  if (file == NULL) {
    kprintf("fd new file is null\n");
    return NULL;
  }

  for (int i = 0; i < fd_number; i++) {
    if (fd_list[i].type == type && fd_list[i].data == file) {
      fd_list[i].use_count++;
      return &fd_list[i];
    }
  }

  fd_list[fd_number].id = fd_number;
  fd_list[fd_number].type = type;
  fd_list[fd_number].data = file;
  fd_list[fd_number].offset = 0;
  fd_list[fd_number].name = kmalloc(kstrlen(name), KERNEL_TYPE);
  kstrcpy(fd_list[fd_number].name, name);
  fd_list[fd_number].use_count = 1;
  return &fd_list[fd_number++];
}

int fd_std_init() {
  char* stdin = "/dev/stdin";
  vnode_t* file = vfs_open_attr(NULL, stdin, V_CHARDEVICE);
  fd_t* fdstdin = fd_open(file, DEVICE_TYPE_FILE, stdin);
  if (fdstdin == NULL) {
    kprintf(" new fd stdin error\n");
    return -1;
  }
  char* stdout = "/dev/stdout";
  file = vfs_open_attr(NULL, stdout, V_CHARDEVICE);
  fd_t* fdstdout = fd_open(file, DEVICE_TYPE_FILE, stdout);
  if (fdstdout == NULL) {
    kprintf(" new fd stdout error\n");
    return -1;
  }

  char* stderro = "/dev/stderr";
  fd_t* fdstderro = fd_open(file, DEVICE_TYPE_FILE, stderro);
  if (fdstderro == NULL) {
    kprintf(" new fd stderro error\n");
    return -1;
  }
  return 1;
}
int fd_init() { return 1; }

int fd_close(fd_t* fd) {
  if (fd == NULL) {
    kprintf("fd close is null\n");
    return -1;
  }
  fd->use_count--;
  if (fd->use_count <= 0) {
    vnode_t* file = fd->data;
    if (file == NULL) {
      log_error("sys close node is null ,name is  %s\n", file->name);
      return -1;
    }
    // reset offset
    // fd->offset = 0;
    // u32 ret = vclose(file);
    return 1;
  }
}

void fd_dumps() {
  for (int i = 0; i < fd_number; i++) {
    fd_t* fd = &fd_list[i];
    kprintf("fd %d name %s id %d %x\n", i, fd->name, fd->id, fd);
  }
}