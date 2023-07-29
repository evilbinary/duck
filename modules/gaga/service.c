/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "service.h"

#include "posix/sysfn.h"

client_t* clients[MAX_CLIENTS];
int client_number = 0;

int service_get_client_no(client_t* client) {
  for (int i = 0; i < client_number; i++) {
    if (clients[i] == client) {
      return i;
    }
  }
  return -1;
}

client_t* service_get_client_name(char* name) {
  for (int i = 0; i < client_number; i++) {
    if (kstrcmp(clients[i]->name, name) == 0) {
      return clients[i];
    }
  }
  return NULL;
}

client_t* service_get_client(int id) {
  for (int i = 0; i < client_number; i++) {
    if (clients[i]->id == i) {
      return clients[i];
    }
  }
  return NULL;
}

int service_add_client(client_t* client) {
  client->id = client_number;
  clients[client_number++] = client;
  return client->id;
}

client_t* service_create_client(char* name, int size) {
  client_t* client = kmalloc(sizeof(client_t), DEVICE_TYPE);
  int len = kstrlen(name);
  client->name = kmalloc(len, DEVICE_TYPE);
  kstrncpy(client->name, name, len);
  client->state = SYS_INIT;
  // alloc apis
  client->api_size = size;
  client->apis = kmalloc(sizeof(api_t) * client->api_size, DEVICE_TYPE);
  log_debug("create client %s %d %x\n", name, client->api_size, client->apis);
  return client;
}

void service_map_client_api(client_t* src, client_t* des) {
  if (src == NULL) {
    log_error("map client src is null\n");
    return;
  }
  if (des == NULL) {
    log_error("map client des is null\n");
    return;
  }
  thread_t* current = thread_current();

  int len = sizeof(api_t) * src->api_size;
  des->api_size = src->api_size;
  des->apis = sys_mmap2(src->apis, len, 0, MAP_ANON, 0, 0);
  page_map_on(current->vm->upage, des->apis, src->apis,
              PAGE_P | PAGE_USR | PAGE_RWX);
  log_debug("mmap addr %x to %x api_size %d\n", src->apis, des->apis,
            des->api_size);
}

void service_remove_client(client_t* client) {
  int pos = service_get_client_no(client);
  for (int i = pos + 1; (i + 1) < client_number; i++) {
    clients[i] = clients[i + 1];
  }
}

u32 service_open(vnode_t* node, u32 mode) { return 0; }

u32 service_close(vnode_t* node) { return 0; }

size_t service_read(vnode_t* node, void* buf, size_t len) {
  u32 ret = 0;
  return ret;
}

size_t service_write(vnode_t* node, const void* buf, size_t len) {
  u32 ret = 0;
  return ret;
}

size_t service_ioctl(vnode_t* node, u32 cmd, void* args) {
  u32 ret = 0;
  if (node == NULL) {
    kprintf("not found node\n");
    return ret;
  }
  thread_t* current = thread_current();

  log_debug("ioctl cmd= %x\n", cmd);

  client_ctl_t* ctl = args;

  if (cmd == SYS_GET_CLIENT) {
    client_t* client = service_get_client_name(ctl->name);
    if (client == NULL) {
      return -1;
    }
    kmemcpy(ctl->client, client, sizeof(client_t));
    service_map_client_api(client, ctl->client);

  } else if (cmd == SYS_NEW_CLIENT) {
    client_t* client = service_create_client(ctl->name, 1);
    int no = service_add_client(client);
    kmemcpy(ctl->client, client, sizeof(client_t));
    service_map_client_api(client, ctl->client);
    ret = no;
    // mmap
  } else if (cmd == SYS_DEL_CLIENT) {
    int id = args;
    client_t* client = service_get_client(id);
    client->state = SYS_DINIT;
    service_remove_client(client);
  }
  return ret;
}

vnode_t* service_find(vnode_t* node, char* name) { return NULL; }