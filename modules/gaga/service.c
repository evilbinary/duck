/*******************************************************************
 * Copyright 2021-present evilbinary
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
  client_t* client = NULL;
  int count = 0;
  for (int i = 0; i < client_number; i++) {
    if (kstrcmp(clients[i]->name, name) == 0) {
      thread_t* t = thread_find_id(clients[i]->tid);
      if (t->state == THREAD_RUNNING) {
        if (client == NULL) {
          client = clients[i];
          client->count = 0;
        } else {
          client->instance[client->count++] = clients[i];
        }
      }
    }
  }
  return client;
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
  client->tid = thread_current()->id;
  // alloc apis
  client->api_size = size;
  client->apis = sys_mmap2(NULL, PAGE_SIZE, 0, MAP_ANON, 0, 0);
  // client->apis = kmalloc(sizeof(api_t) * client->api_size, DEVICE_TYPE);
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

  char* name = des->name;
  kmemcpy(des, src, sizeof(client_t));
  des->name = name;

  int len = sizeof(api_t) * src->api_size;
  des->api_size = src->api_size;

  thread_t* t = thread_find_id(src->tid);
  if (t == NULL) {
    log_error("service thread is null\n");
    return;
  }

  void* addr = page_v2p(t->vm->upage, src->apis);
  if (addr == NULL) {
    log_warn("addr is null %s\n", t->name);
    return;
  }
  des->apis = sys_mmap2(NULL, PAGE_SIZE, 0, MAP_ANON, 0, 0);
  page_map_on(current->vm->upage, des->apis, addr,
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

void service_client_dump(client_t* client) {
  if (client == NULL) {
    return;
  }
  for (int i = 0; i < client->api_size; i++) {
    api_t* api = &client->apis[i];
    log_debug("fn: %d state: %d ret: %d args: %x\n", api->fn, api->state,
              api->ret, api->args);
  }
}

void do_dump_thread() {
  for (;;) {
    client_t* client = service_get_client_name("serviceb");
    service_client_dump(client);
    u32 tv[2] = {5, 0};
    sys_clock_nanosleep(0, 0, &tv, &tv);
  }
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
      log_error("client is null\n");
      return -1;
    }
    client->cid = current->id;
    service_map_client_api(client, ctl->client);
    // thread_t* t1 = thread_create_name_level("dump", (u32*)&do_dump_thread,
    // NULL,
    //                                         LEVEL_KERNEL_SHARE);
    // thread_run(t1);
  } else if (cmd == SYS_NEW_CLIENT) {
    client_t* client = service_create_client(ctl->name, 1);
    client->cid = current->id;
    int no = service_add_client(client);
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