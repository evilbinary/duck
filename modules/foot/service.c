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
      if (t->state != THREAD_STOPPED) {
        if (client == NULL) {
          client = clients[i];
          client->count = 0;
        } else if (client->count < MAX_INSTANCE) {
          client->instance[client->count++] = clients[i];
        }
      }
    }
  }
  return client;
}

client_t* service_get_client(int id) {
  for (int i = 0; i < client_number; i++) {
    if (clients[i]->id == id) {
      return clients[i];
    }
  }
  return NULL;
}

int service_add_client(client_t* client) {
  if (client_number >= MAX_CLIENTS) {
    log_error("clients table full (%d)\n", MAX_CLIENTS);
    return -1;
  }
  client->id = client_number;
  clients[client_number++] = client;
  return client->id;
}

/* 共享页页数：槽数组 + 尾部控制区（kick 字），向上取整，至少 1 页 */
static int service_api_pages(int slots) {
  int bytes = slots * (int)sizeof(api_t) + FOOT_CTL_BYTES;
  int pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
  return pages < 1 ? 1 : pages;
}

client_t* service_create_client(char* name, int size) {
  if (size < 1) size = 1;
  if (size > MAX_API_SLOTS) size = MAX_API_SLOTS;

  client_t* client = kmalloc(sizeof(client_t), DEVICE_TYPE);
  kmemset(client, 0, sizeof(client_t));

  int len = kstrlen(name);
  client->name = kmalloc(len + 1, DEVICE_TYPE);
  kmemset(client->name, 0, len + 1);
  kstrncpy(client->name, name, len);

  client->state = SYS_INIT;
  client->tid = thread_current()->id;
  client->api_size = size;

  /* 共享页：调用方与服务方映射同一物理页，槽位数组就在这里 */
  int pages = service_api_pages(size);
  client->apis = sys_mmap2(NULL, pages * PAGE_SIZE, 0, MAP_ANON, 0, 0);
  if (client->apis != NULL) {
    kmemset(client->apis, 0, pages * PAGE_SIZE);
  }
  log_debug("create client %s slots=%d apis=%x\n", name, client->api_size,
            client->apis);
  return client;
}

/* self=1：服务方自己注册时的映射（不计入 map_count）；
 * self=0：外部进程（client_get）持有的映射（计入 map_count，注销时不回收）。 */
void service_map_client_api(client_t* src, client_t* des, int self) {
  if (src == NULL) {
    log_error("map client src is null\n");
    return;
  }
  if (des == NULL) {
    log_error("map client des is null, src id %d\n", src->id);
    return;
  }
  thread_t* current = thread_current();

  /* des 是用户态结构：内核不裸读/裸写，统一走 copy_from/to_user 做逐页校验 */
  client_t udes;
  if (sys_copy_from_user(&udes, des, sizeof(client_t)) < 0) {
    log_error("map client: copy_from_user failed\n");
    return;
  }

  thread_t* t = thread_find_id(src->tid);
  if (t == NULL) {
    log_error("service thread is null\n");
    return;
  }

  int pages = service_api_pages(src->api_size);

  client_t kdes;
  kmemcpy(&kdes, src, sizeof(client_t));
  kdes.name = udes.name; /* 保留调用方自己的 name 指针 */
  kdes.apis = sys_mmap2(NULL, pages * PAGE_SIZE, 0, MAP_ANON, 0, 0);
  if (kdes.apis == NULL) {
    log_error("map client: mmap %d pages failed\n", pages);
    return;
  }

  /* 逐页把服务方的共享页物理帧映射进调用方地址空间 */
  for (int i = 0; i < pages; i++) {
    void* pa = page_v2p(t->vm->upage, (char*)src->apis + i * PAGE_SIZE);
    if (pa == NULL) {
      log_warn("map client: apis page %d unmapped (%s)\n", i, t->name);
      break;
    }
    page_map_on(current->vm->upage, (char*)kdes.apis + i * PAGE_SIZE, pa,
                PAGE_USER);
  }

  if (sys_copy_to_user(des, &kdes, sizeof(client_t)) < 0) {
    log_error("map client: copy_to_user failed\n");
    return;
  }
  /* 记录"共享页被外部进程映射过"：只要还有人映射着，注销时就不能释放物理帧 */
  if (!self) {
    __atomic_add_fetch(&src->map_count, 1, __ATOMIC_ACQ_REL);
  }
  log_debug("mmap client %s apis %x -> %x slots=%d pages=%d maps=%d\n",
            src->name, src->apis, kdes.apis, kdes.api_size, pages,
            src->map_count);
}

void service_remove_client(client_t* client) {
  int pos = service_get_client_no(client);
  if (pos < 0) {
    log_warn("remove client not found id %d\n", client->id);
    return;
  }
  for (int i = pos; (i + 1) < client_number; i++) {
    clients[i] = clients[i + 1];
  }
  if (client_number > 0) {
    clients[--client_number] = NULL;
  }

  /* 【注销回收】只有满足两条才真释放，否则只摘名字（保守泄漏）：
   *  1) 没有外部进程映射过共享页（map_count==0）—— 别人还映射着就 free，
   *     那些进程会踩到被复用的内存；
   *  2) 当前线程就是注册者 —— 只有注册者的 vm 里有这个映射，munmap 才有意义。 */
  if (client->apis != NULL &&
      __atomic_load_n(&client->map_count, __ATOMIC_ACQUIRE) == 0 &&
      client->tid == thread_current()->id) {
    int pages = service_api_pages(client->api_size);
    sys_munmap(client->apis, (size_t)pages * PAGE_SIZE);
    if (client->name != NULL) {
      kfree(client->name);
    }
    kfree(client);
  } else if (client->name != NULL) {
    log_debug("remove client %s kept (maps=%d owner=%d self=%d)\n",
              client->name, client->map_count, client->tid,
              thread_current()->id);
  }
}

u32 service_open(vnode_t* node, u32 mode) { return 0; }

/* 关闭 /dev/foot：把该线程注册过的所有 client 摘除，
 * 否则客户端崩掉后 clients[] 会被永久占满（MAX_CLIENTS=100）。 */
u32 service_close(vnode_t* node) {
  thread_t* current = thread_current();
  if (current == NULL) return 0;

  int i = 0;
  int removed = 0;
  while (i < client_number) {
    if (clients[i] != NULL && clients[i]->tid == current->id) {
      client_t* victim = clients[i];
      /* 顺手摘掉 /dev/foot 下的子节点，否则每注册一次就漏一个 vnode */
      if (victim->name != NULL) {
        vnode_t* node_client = vfs_find_child(node, victim->name);
        if (node_client != NULL) {
          vfs_remove_child(node, node_client);
        }
      }
      victim->state = SYS_DINIT;
      service_remove_client(victim); /* 内含数组搬移、client_number--、回收 */
      removed++;
    } else {
      i++;
    }
  }
  if (removed > 0) {
    log_debug("close: removed %d client(s) of tid %d, %d left\n", removed,
              current->id, client_number);
  }
  return 0;
}

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

ssize_t service_ioctl(vnode_t* node, u32 cmd, void* args) {
  u32 ret = 0;
  if (node == NULL) {
    kprintf("not found node\n");
    return ret;
  }
  thread_t* current = thread_current();

  log_debug("current thread name %s\n", current->name);

  // log_debug("ioctl cmd= %x args= %x\n", cmd, args);

  /* 用户参数：先拷进内核，之后只用内核副本解释 */
  client_ctl_t uctl;
  if (sys_copy_from_user(&uctl, args, sizeof(uctl)) < 0) {
    log_error("ioctl: copy_from_user ctl failed\n");
    return -1;
  }

  char kname[32];
  kmemset(kname, 0, sizeof(kname));
  if (uctl.name != NULL) {
    if (sys_copy_from_user(kname, uctl.name, sizeof(kname) - 1) < 0) {
      log_error("ioctl: copy_from_user name failed\n");
      return -1;
    }
  }

  if (cmd == SYS_GET_CLIENT) {
    client_t* client = service_get_client_name(kname);
    if (client == NULL) {
      log_error("client %s get is null\n", kname);
      return -1;
    }
    client->cid = current->id;
    service_map_client_api(client, uctl.client, 0);
  } else if (cmd == SYS_NEW_CLIENT) {
    int slots = uctl.api_size;
    if (slots <= 0 || slots > MAX_API_SLOTS) slots = FOOT_API_DEFAULT;
    client_t* client = service_create_client(kname, slots);
    if (client == NULL) {
      log_error("create client %s failed\n", kname);
      return -1;
    }
    client->cid = current->id;
    int no = service_add_client(client);
    if (no < 0) {
      return -1;
    }
    service_map_client_api(client, uctl.client, 1);

    vnode_t* node_client = vfs_create_node(client->name, V_FILE);
    vfs_add_child(node, node_client);

    ret = no;
  } else if (cmd == SYS_DEL_CLIENT) {
    int id = uctl.id;
    client_t* client = service_get_client(id);
    if (client == NULL) {
      log_error("client del id %d is null\n", id);
      return -1;
    }
    /* 先摘 vfs 子节点（要用 client->name），再摘 clients[] 表——
     * service_remove_client 在无外部映射时会连 name 一起释放。 */
    {
      vnode_t* node_client = vfs_find_child(node, client->name);
      if (node_client != NULL) {
        vfs_remove_child(node, node_client);
      }
    }
    client->state = SYS_DINIT;
    service_remove_client(client);
  } else if (cmd == SYS_GET_CLIENT_BY_ID) {
    int id = uctl.id;
    client_t* client = service_get_client(id);
    if (client == NULL) {
      log_error("client get id %d is null\n", id);
      return -1;
    }
    client->cid = current->id;
    service_map_client_api(client, uctl.client, 0);
  }
  return ret;
}

vnode_t* service_find(vnode_t* node, char* name) { return NULL; }

u32 service_readdir(vnode_t* node, struct vdirent* dirent, u32* offset,
                    u32 count) {
  if (!((node->flags & V_FILE) == V_FILE ||
        (node->flags & V_DIRECTORY) == V_DIRECTORY)) {
    log_debug("read dir failed for not file flags is %x\n", node->flags);
    return 0;
  }
  u32 i = 0;
  u32 nbytes = 0;
  u32 read_count = 0;
  while (i < node->child_number) {
    if (i < *offset) {  // 定位到某个文件数量开始
      i++;
      continue;
    }
    if (read_count < count) {
      dirent->type = DT_REG;
      kstrcpy(dirent->name, node->child[i]);
      dirent->offset = i;
      dirent->length = sizeof(struct vdirent);
      nbytes += dirent->length;
      dirent++;  // maybe change to offset
      (*offset)++;
      read_count++;
    } else {
      break;
    }
    i++;
  }

  return nbytes;
}
