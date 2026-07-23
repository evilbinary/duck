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

int fd_init() {
  for (int i = 0; i < MAX_FD_NUMBER; i++) {
    fd_list[i].use_count = -1;  // -1 = never allocated
  }

  fd_std_init();
  return 1;
}

fd_t* fd_find(u32 fd) {
  for (int i = 0; i < fd_number; i++) {
    if (fd_list[i].id == fd && fd_list[i].data != NULL) {
      return &fd_list[i];
    }
  }
  return NULL;
}

static fd_t* fd_alloc_slot(u32* file, u32 type, char* name, int id) {
  fd_t* slot = &fd_list[id];
  slot->id = id;
  slot->type = type;
  slot->data = file;
  slot->offset = 0;
  slot->name = kmalloc(kstrlen(name) + 1, KERNEL_TYPE);
  kstrcpy(slot->name, name);
  slot->use_count = 0;  // incremented by thread_add_fd
  slot->flags = 0;
  return slot;
}

fd_t* fd_open(u32* file, u32 type, char* name) {
  if (file == NULL) {
    kprintf("fd new file is null\n");
    return NULL;
  }
  if (fd_number >= MAX_FD_NUMBER) {
    kprintf("new fd limit\n");
    return NULL;
  }
  return fd_alloc_slot(file, type, name, fd_number++);
}

static int fd_reopen_stdio_slot(int id, char* path) {
  vnode_t* file = vfs_open_attr(NULL, path, V_CHARDEVICE);
  if (file == NULL) {
    return -1;
  }

  if (id >= MAX_FD_NUMBER) {
    return -1;
  }

  if (id >= fd_number) {
    fd_number = id + 1;
  }

  fd_t* slot = &fd_list[id];
  if (slot->name != NULL) {
    kfree(slot->name);
    slot->name = NULL;
  }
  fd_alloc_slot((u32*)file, DEVICE_TYPE_FILE, path, id);
  return 0;
}

int fd_ensure_stdio() {
  static char* stdio_path[] = {"/dev/stdin", "/dev/stdout", "/dev/stderr"};

  for (int i = STDIN; i <= STDERR; i++) {
    fd_t* fd = NULL;
    if (i < fd_number) {
      fd = &fd_list[i];
    }
    if (fd == NULL || fd->data == NULL) {
      if (fd_reopen_stdio_slot(i, stdio_path[i]) < 0) {
        return -1;
      }
    }
  }
  return 0;
}

int fd_std_init() {
  return fd_ensure_stdio();
}
int fd_close(fd_t* fd) {
  if (fd == NULL) {
    kprintf("fd close is null\n");
    return -1;
  }
  if ((u32)fd < PAGE_SIZE) {
    log_error("fd close bad ptr %x\n", fd);
    return -1;
  }
  fd->use_count--;
  if (fd->use_count <= 0) {
    vnode_t* file = (vnode_t*)fd->data;
    if (file != NULL) {
      vclose(file);
    }
    fd->data = NULL;
    fd->use_count = 0;
  }
  return 0;
}

void fd_dumps() {
  for (int i = 0; i < fd_number; i++) {
    fd_t* fd = &fd_list[i];
    kprintf("fd %d name %s id %d %x\n", i, fd->name, fd->id, fd);
  }
}