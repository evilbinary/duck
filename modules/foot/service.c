/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "service.h"

#include "kernel/cap.h"
#include "kernel/cred.h"
#include "kernel/thread_event.h"
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
    /* 控制区 alive=1：调用方据此判断服务端是否还在（统计字由调用方维护） */
    ((u32*)((char*)client->apis + (int)size * (int)sizeof(api_t)))
        [FOOT_CTL_ALIVE] = 1;
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

  /* 先给共享页盖上"服务端已走"的戳：还在等结果的调用方醒来后能立刻判死
   * （页若随后被回收，这次写也在回收之前）。 */
  if (client->apis != NULL) {
    ((u32*)((char*)client->apis + (int)client->api_size * (int)sizeof(api_t)))
        [FOOT_CTL_ALIVE] = 0;
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

/* 当前线程的 CSpace（cap 核心在 kernel/cap.c，v2 挂 TCB） */
static cspace_t* service_caps(void) { return cap_current(); }

/* /dev/foot 的 capability 命令：申请句柄 / 映射 / 查询 / 派生 / 撤销。
 * 参数一律先拷进内核再解释（用户指针不裸用）。
 * v2：能力路径不看 uid —— 授权由“有没有能力”决定，身份只进审计日志。 */
static ssize_t service_cap_ioctl(u32 cmd, void* args) {
  thread_t* current = thread_current(); /* 只为拿 upage（映射目标页表） */
  cap_ctl_t uctl;

  if (args == NULL) {
    return -1;
  }
  if (sys_copy_from_user(&uctl, args, sizeof(uctl)) < 0) {
    log_error("cap ioctl: copy ctl failed\n");
    return -1;
  }
  cspace_t* cs = service_caps();
  if (cs == NULL) {
    return -1;
  }
  void* pgdir = (current != NULL && current->vm != NULL) ? current->vm->upage
                                                         : NULL;

  int ret = -1;
  switch (cmd) {
    case SYS_CAP_GET_MMIO: {
      char kname[32];
      kmemset(kname, 0, sizeof(kname));
      if (uctl.name == NULL) {
        break;
      }
      if (sys_copy_from_user(kname, uctl.name, sizeof(kname) - 1) < 0) {
        break;
      }
      /* v2 过渡：按名字把窗口对象的一个能力装进调用者 CSpace。
       * 最终形态是 boot 授权表预置 + cap_copy 转授，按名字申请只是过渡口。 */
      u32 h = cap_devwin_grant(cs, kname);
      if (h == 0) {
        log_warn("cap: devwin '%s' not available\n", kname);
        break;
      }
      uctl.handle = h;
      ret = 0;
    } break;
    case SYS_CAP_MAP:
      /* 按句柄的实际类型分派（设备窗口 / 物理页帧 / 将来的其它可映射对象） */
      ret = cap_map(cs, uctl.handle, pgdir, uctl.vaddr, uctl.offset, uctl.len);
      break;
    case SYS_CAP_UNMAP:
      ret = cap_unmap(cs, uctl.handle, pgdir);
      break;
    case SYS_CAP_INFO:
      ret = cap_res_info(cs, uctl.handle, &uctl.pa, &uctl.size, &uctl.rights);
      break;
    case SYS_CAP_ALLOC_FRAME: {
      /* 入参：size（0 = 一页）、pa（0 = 新分配）、rights（0 = 默认） */
      u32 h = cap_frame_alloc(cs, uctl.size, uctl.pa, uctl.rights);
      if (h == 0) {
        break;
      }
      uctl.handle = h;
      /* 回填实际物理基址与长度，用户态不必再发一次 INFO */
      cap_frame_info(cs, h, &uctl.pa, &uctl.size, &uctl.rights);
      ret = 0;
    } break;
    case SYS_CAP_ALLOC_ARENA: {
      /* 入参：size=长度、offset=物理对齐（页向上取整，0=页对齐）、
       * rights=0 取默认。物理连续大块（GPU 页表等），走 phy_alloc_aligment。 */
      u32 h = cap_arena_alloc(cs, uctl.size, uctl.offset, uctl.rights);
      if (h == 0) {
        break;
      }
      uctl.handle = h;
      cap_arena_info(cs, h, &uctl.pa, &uctl.size, &uctl.rights);
      ret = 0;
    } break;
    case SYS_CAP_DERIVE: {
      u32 h = cap_mint(cs, uctl.handle, uctl.rights);
      if (h == 0) {
        break;
      }
      uctl.handle = h;
      ret = 0;
    } break;
    case SYS_CAP_REVOKE:
      cap_revoke(cs, uctl.handle, pgdir);
      ret = 0;
      break;
    default:
      break;
  }

  /* 回填（handle/pa/size/rights 是输出；失败时用户也能看到 -1） */
  if (sys_copy_to_user(args, &uctl, sizeof(uctl)) < 0) {
    return -1;
  }
  return ret;
}

/* 端点收发的阻塞等待块。挂起后本函数【不会返回】，所以参数放内核堆，
 * 就绪时由 poll_wait_suspend 的重查在等待者页表下完成用户态回写。
 * 结构与用法与 posix 的 sys_rw_wait 完全一致（sysfn_fs.c）。 */
struct cap_ep_wait {
  cspace_t* cs;
  cap_ep_ctl_t uc; /* 用户参数副本（含 buf 指针与输出字段） */
  void* uargs;     /* 用户参数地址：就绪时整体写回 */
  int is_recv;     /* 1 = 接收方向，0 = 发送方向 */
  int done;        /* 已完成的标记：唤醒遍历可能重复调用本回调，必须幂等 */
  u32 len;         /* send：数据长度；recv：可接收容量 */
  u32 rights;
  u32 grant_h; /* send：待转授句柄 */
  char data[CAP_EP_DATA_MAX];
};

/* 队列空/满返回 0（继续等）；能完成就地做完并回写用户态，返回 1。
 * 出错返回负值表示放弃等待（该值直接成为 syscall 返回值）。
 * 完成后再被遍历到要直接返回 1，否则会重复入队/重复取消息。
 * 这里传 wake=1：入队/出队后要叫醒对端；cap_endpoint 内部的 busy/pending
 * 循环把"重查里再唤醒"的递归折成了循环。 */
static int cap_ep_rw_recheck(void* a) {
  struct cap_ep_wait* w = (struct cap_ep_wait*)a;
  if (w->done) {
    return 1;
  }
  if (!w->is_recv) {
    if (cap_endpoint_send_ex(w->cs, w->uc.handle, w->data, w->len, w->grant_h,
                             w->rights, 1) != 0) {
      return 0; /* 队列满 */
    }
    w->done = 1;
    return 1;
  }
  u32 out_len = 0, out_grant = 0;
  if (cap_endpoint_recv_ex(w->cs, w->uc.handle, w->data, w->len, &out_len,
                           &out_grant, 1) != 0) {
    return 0; /* 队列空 */
  }
  if (out_len > 0 && w->uc.buf != NULL) {
    if (sys_copy_to_user(w->uc.buf, w->data, out_len) < 0) {
      return -1;
    }
  }
  w->uc.out_len = out_len;
  w->uc.grant = out_grant;
  if (w->uargs != NULL) {
    if (sys_copy_to_user(w->uargs, &w->uc, sizeof(w->uc)) < 0) {
      return -1;
    }
  }
  w->done = 1;
  return 1;
}

static void cap_ep_rw_release(void* a) { kfree(a); }

/* 端点（IPC）命令：参数是 cap_ep_ctl_t，收发数据要在用户/内核之间搬。
 * 阻塞语义沿用 posix 的 sys_rw_wait：登记 recheck → THREAD_WAITING →
 * schedule_switch，对端入队/出队时经 thread_event_notify_hook 唤醒。 */
static ssize_t service_cap_ep_ioctl(u32 cmd, void* args) {
  cap_ep_ctl_t ctl;
  if (args == NULL) {
    return -1;
  }
  if (sys_copy_from_user(&ctl, args, sizeof(ctl)) < 0) {
    return -1;
  }
  cspace_t* cs = service_caps();
  if (cs == NULL) {
    return -1;
  }
  int ret = -1;
  switch (cmd) {
    case SYS_CAP_EP_CREATE: {
      u32 h = cap_endpoint_create(cs);
      if (h == 0) {
        break;
      }
      ctl.handle = h;
      ret = 0;
    } break;
    case SYS_CAP_EP_SEND:
    case SYS_CAP_EP_RECV: {
      int is_recv = (cmd == SYS_CAP_EP_RECV);
      u32 want = ctl.len;
      if (want > CAP_EP_DATA_MAX) {
        want = CAP_EP_DATA_MAX;
      }
      /* 非阻塞（或调用方显式要求）：直接试一次，满/空就失败 */
      if ((ctl.flags & CAP_EP_NONBLOCK) != 0) {
        char kbuf[CAP_EP_DATA_MAX];
        kmemset(kbuf, 0, sizeof(kbuf));
        if (!is_recv) {
          if (want > 0 &&
              (ctl.buf == NULL || sys_copy_from_user(kbuf, ctl.buf, want) < 0)) {
            break;
          }
          ret = cap_endpoint_send(cs, ctl.handle, kbuf, want, ctl.grant,
                                  ctl.rights);
        } else {
          u32 out_len = 0, out_grant = 0;
          ret = cap_endpoint_recv(cs, ctl.handle, kbuf, want, &out_len,
                                  &out_grant);
          if (ret == 0) {
            ctl.out_len = out_len;
            ctl.grant = out_grant;
            if (out_len > 0 && ctl.buf != NULL &&
                sys_copy_to_user(ctl.buf, kbuf, out_len) < 0) {
              ret = -1;
            }
          }
        }
        break;
      }

      /* 阻塞：登记 → 挂起；队列已就绪时根本不睡。 */
      struct cap_ep_wait* w = kmalloc(sizeof(*w), KERNEL_TYPE);
      if (w == NULL) {
        break;
      }
      kmemset(w, 0, sizeof(*w));
      w->cs = cs;
      w->uc = ctl;
      w->uargs = args;
      w->is_recv = is_recv;
      w->len = want;
      w->rights = ctl.rights;
      w->grant_h = ctl.grant;
      if (!is_recv && want > 0 && ctl.buf != NULL &&
          sys_copy_from_user(w->data, ctl.buf, want) < 0) {
        kfree(w);
        break;
      }
      /* 就绪时 recheck 返回 1（result 为 NULL ⇒ 这个 1 就是 syscall 返回值，
       * libfoot 按 "<0 即失败" 判定，所以 1 也表示成功）。真挂起后不返回，
       * 唤醒方把 1 写进保存帧 x0。 */
      /* 定向等待：挂到这个端点自己的事件上，队列变化时只唤醒它，
       * 不必让全表的每个等待者都重跑一次 recheck。 */
      int r = thread_event_wait_on((thread_event_t*)cap_endpoint_event(
                                       cs, ctl.handle),
                                   cap_ep_rw_recheck, NULL, cap_ep_rw_release,
                                   w, 0, 0);
      /* 能走到这里只有两种情况：本来就有事件（r>0）或等待表满（r==0，
       * 此时 w 已被 release）。信号打断返回负值。 */
      if (r < 0) {
        return r;
      }
      return r > 0 ? 0 : -1;
    }
    default:
      break;
  }
  if (sys_copy_to_user(args, &ctl, sizeof(ctl)) < 0) {
    return -1;
  }
  return ret;
}

ssize_t service_ioctl(vnode_t* node, u32 cmd, void* args) {
  u32 ret = 0;
  /* capability 命令：参数结构、语义都与客户端命令不同，先分流 */
  if (cmd >= SYS_CAP_EP_CREATE && cmd <= SYS_CAP_EP_RECV) {
    return service_cap_ep_ioctl(cmd, args);
  }
  if (cmd >= SYS_CAP_GET_MMIO && cmd <= SYS_CAP_ALLOC_FRAME) {
    return service_cap_ioctl(cmd, args);
  }
  /* 物理连续大块分配（命令号在 EP 区间之后，单独路由） */
  if (cmd == SYS_CAP_ALLOC_ARENA) {
    return service_cap_ioctl(cmd, args);
  }
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
