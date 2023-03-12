#include "ewok.h"

#define context_t ewok_context_t

static uint32_t _svc_counter[EWOK_SYS_CALL_NUM];
static uint32_t _svc_total;

extern sys_info_t _sys_info;

extern bool _core_proc_ready;
extern int32_t _core_proc_pid;
extern uint32_t _ipc_uid;

int32_t schedule(context_t* ctx) { log_debug("ewok schedule not impl\n"); }


procinfo_t* get_procs(int32_t *num) {
  log_debug("ewok get_procs not impl\n");

  return NULL;
}


static void ewok_sys_kprint(const char* s, int32_t len) {
  (void)len;
  kprintf(s);
}

static void ewok_sys_exit(context_t* ctx, int32_t res) {
  ctx->gpr[0] = 0;
  sys_exit(0);
}

static int32_t ewok_sys_signal_setup(uint32_t entry) {
  log_debug("ewok_sys_signal_setup not impl\n");
  return 0;
}

static void ewok_sys_signal(context_t* ctx, int32_t pid, int32_t sig) {
  log_debug("ewok_sys_signal not impl\n");
}

static void ewok_sys_signal_end(context_t* ctx) {
  log_debug("sys_signal_end not impl\n");
}

static int32_t ewok_sys_getpid(int32_t pid) {
  thread_t* current = thread_current();
  return current->pid;
}

static int32_t ewok_sys_get_thread_id(void) {
  thread_t* current = thread_current();
  return current->id;
}

static void ewok_sys_usleep(context_t* ctx, uint32_t count) {
  u32 tv[2] = {1, 0};
  sys_nanosleep(&tv, &tv);
}

static int32_t ewok_sys_malloc(int32_t size) {
  log_debug("ewok_sys_malloc %d\n", size);
  return kmalloc(size, DEFAULT_TYPE);
}

static int32_t ewok_sys_realloc(void* p, int32_t size) {
  if (p == NULL) {
    return kmalloc(size, DEFAULT_TYPE);
  }
  int psize = kmem_size(p);
  if (psize >= size) {
    return p;
  }
  void* new_p = kmalloc(size, DEFAULT_TYPE);
  kmemcpy(new_p, p, psize);
  kfree(p);
  return new_p;
}

static void ewok_sys_free(int32_t p) {
  if (p == 0) return;
  kfree(p);
}

static void ewok_sys_fork(context_t* ctx) {
  log_debug("ewok_sys_fork\n");
  sys_fork();
}

static void ewok_sys_detach(void) { log_debug("ewok_sys_detach\n"); }

static void ewok_sys_thread(context_t* ctx, uint32_t entry, uint32_t func,
                            int32_t arg) {
  log_debug("ewok_sys_thread not impl\n");
}

static void ewok_sys_waitpid(context_t* ctx, int32_t pid) {
  log_debug("ewok_sys_waitpid not impl\n");
}

static void ewok_sys_load_elf(context_t* ctx, const char* cmd, void* elf,
                              uint32_t elf_size) {
  log_debug("ewok_sys_load_elf %s\n", cmd);
  sys_exec(cmd, NULL, NULL);
}

static int32_t ewok_sys_proc_set_uid(int32_t uid) {
  log_debug("ewok_sys_proc_set_uid not impl\n");
  return 0;
}

static int32_t ewok_sys_proc_get_cmd(int32_t pid, char* cmd, int32_t sz) {
  thread_t* current = thread_current();
  if (current == NULL) return -1;
  strncpy(cmd, current->name, sz);
  return 0;
}

static void ewok_sys_proc_set_cmd(const char* cmd) {
  thread_t* current = thread_current();
  if (current == NULL) return -1;
  strcpy(current->name, cmd);
}

static void ewok_sys_get_sys_info(sys_info_t* info) {
  if (info == NULL) return;
  memcpy(info, &_sys_info, sizeof(sys_info_t));
}

static void ewok_sys_get_sys_state(sys_state_t* info) {
  if (info == NULL) return;

  info->mem.free = 0;
  info->mem.shared = 0;
  info->kernel_sec = 0;
  info->svc_total = _svc_total;
  memcpy(info->svc_counter, _svc_counter, EWOK_SYS_CALL_NUM * 4);
}

static int32_t ewok_sys_shm_alloc(uint32_t size, int32_t flag) {
  log_debug("ewok_sys_shm_alloc not impl\n");

  return 0;
}

static void* ewok_sys_shm_map(int32_t id) {
  log_debug("ewok_sys_shm_map not impl\n");
  return 0;
}

static int32_t ewok_sys_shm_unmap(int32_t id) {
  log_debug("ewok_sys_shm_unmap not impl\n");
  return 0;
}

static int32_t ewok_sys_shm_ref(int32_t id) {
  log_debug("ewok_sys_shm_ref not impl\n");
  return 0;
  ;
}

static uint32_t ewok_sys_dma_map(uint32_t size) {
  log_debug("ewok_sys_dma_map not impl\n");
  return 0;
}

static uint32_t ewok_sys_mem_map(uint32_t vaddr, uint32_t paddr,
                                 uint32_t size) {
  log_debug("ewok_sys_mem_map not impl\n");
  return 0;
}

static void ewok_sys_ipc_setup(context_t* ctx, uint32_t entry,
                               uint32_t extra_data, uint32_t flags) {
  log_debug("ewok_sys_ipc_setup not impl\n");

  // proc_t* cproc = get_current_proc();
  // ctx->gpr[0] = proc_ipc_setup(ctx, entry, extra_data, flags);
  // if ((flags & IPC_NON_BLOCK) == 0) {
  //   cproc->info.state = BLOCK;
  //   cproc->info.block_by = cproc->info.pid;
  //   schedule(ctx);
  // }
}

static void ewok_sys_ipc_call(context_t* ctx, int32_t serv_pid, int32_t call_id,
                              proto_t* data) {
  log_debug("ewok_sys_ipc_call not impl\n");

  // ctx->gpr[0] = 0;

  // proc_t* client_proc = get_current_proc();
  // proc_t* serv_proc = proc_get(serv_pid);

  // if (serv_proc == NULL ||
  //     client_proc->info.pid == serv_pid ||      // can't do self ipc
  //     serv_proc->space->ipc_server.entry == 0)  // no ipc service setup
  //   return;

  // if (serv_proc->space->ipc_server.disabled) {
  //   ctx->gpr[0] = -1;  // blocked if server disabled, should retry
  //   proc_block_on(serv_pid, (uint32_t)&serv_proc->space->ipc_server);
  //   schedule(ctx);
  //   return;
  // }

  // if (serv_proc->space->interrupt.state != INTR_STATE_IDLE) {
  //   ctx->gpr[0] = -1;  // blocked if proc is on interrupt task, should retry
  //   proc_block_on(serv_pid, (uint32_t)&serv_proc->space->interrupt);
  //   schedule(ctx);
  //   return;
  // }

  // ipc_task_t* ipc =
  //     proc_ipc_req(serv_proc, client_proc->info.pid, call_id, data);
  // if (ipc == NULL) return;

  // ctx->gpr[0] = ipc->uid;
  // if (ipc != proc_ipc_get_task(serv_proc))  // buffered ipc
  //   return;
  // proc_ipc_do_task(ctx, serv_proc, client_proc->info.core);
}

static void ewok_sys_ipc_get_return(context_t* ctx, int32_t pid, uint32_t uid,
                                    proto_t* data) {
  log_debug("ewok_sys_ipc_get_return not impl\n");

  // ctx->gpr[0] = 0;
  // proc_t* client_proc = get_current_proc();
  // if (uid == 0 || client_proc == NULL) {
  //   ctx->gpr[0] = -2;
  //   return;
  // }

  // if (client_proc->ipc_res.state != IPC_RETURN) {  // block retry for serv
  //                                                  // return
  //   proc_t* serv_proc = proc_get(pid);
  //   ipc_task_t* ipc = proc_ipc_get_task(serv_proc);
  //   if (ipc == NULL) {
  //     ctx->gpr[0] = -2;
  //     return;
  //   }

  //   if ((ipc->call_id & IPC_NON_RETURN) == 0 || ipc->uid != uid) {
  //     ctx->gpr[0] = -1;
  //     // proc_block_on(pid, (uint32_t)&serv_proc->space->ipc_server);
  //     proc_block_on(pid, (uint32_t)&client_proc->ipc_res);
  //     schedule(ctx);
  //     return;
  //   }
  //   return;
  // }

  // if (client_proc->ipc_res.uid != uid) {
  //   ctx->gpr[0] = -2;
  //   return;
  // }

  // if (data != NULL) {  // get return value
  //   if (client_proc->ipc_res.data.size > 0) {
  //     if (data->total_size < client_proc->ipc_res.data.size) {
  //       data->data =
  //           (proto_t*)proc_malloc(client_proc,
  //           client_proc->ipc_res.data.size);
  //       data->total_size = client_proc->ipc_res.data.size;
  //     }
  //     data->size = client_proc->ipc_res.data.size;
  //     memcpy(data->data, client_proc->ipc_res.data.data, data->size);
  //   }
  // }

  // client_proc->ipc_res.uid = 0;
  // client_proc->ipc_res.state = IPC_IDLE;
  // proto_clear(&client_proc->ipc_res.data);
}

static int32_t ewok_sys_ipc_get_info(uint32_t uid, int32_t* ipc_info,
                                     proto_t* ipc_arg) {
  log_debug("ewok_sys_ipc_get_info not impl\n");

  // proc_t* serv_proc = get_current_proc();
  // ipc_task_t* ipc = proc_ipc_get_task(serv_proc);
  // if (uid == 0 || ipc == NULL || ipc->uid != uid || ipc->state != IPC_BUSY ||
  //     serv_proc->space->ipc_server.entry == 0) {
  //   return -1;
  // }

  // ipc_info[0] = ipc->client_pid;
  // ipc_info[1] = ipc->call_id;

  // if (ipc->data.size > 0) {  // get request input args
  //   ipc_arg->size = ipc->data.size;
  //   if (ipc_arg->total_size < ipc->data.size) {
  //     ipc_arg->data = proc_malloc(serv_proc, ipc->data.size);
  //     ipc_arg->total_size = ipc->data.size;
  //   }
  //   memcpy(ipc_arg->data, ipc->data.data, ipc->data.size);
  //   proto_clear(&ipc->data);
  // }

  return 0;
}

static void ewok_sys_ipc_set_return(context_t* ctx, uint32_t uid,
                                    proto_t* data) {
  log_debug("ewok_sys_ipc_set_return not impl\n");

  // proc_t* serv_proc = get_current_proc();
  // ipc_task_t* ipc = proc_ipc_get_task(serv_proc);
  // if (uid == 0 || ipc == NULL || ipc->uid != uid ||
  //     serv_proc->space->ipc_server.entry == 0 || ipc->state != IPC_BUSY ||
  //     (ipc->call_id & IPC_NON_RETURN) != 0) {
  //   return;
  // }

  // proc_t* client_proc = proc_get(ipc->client_pid);
  // if (client_proc != NULL) {
  //   client_proc->ipc_res.state = IPC_RETURN;
  //   client_proc->ipc_res.uid = uid;
  //   if (data != NULL) {
  //     proto_copy(&client_proc->ipc_res.data, data->data, data->size);
  //   }
  //   proc_wakeup(serv_proc->info.pid, (uint32_t)&client_proc->ipc_res);
  //   proc_switch_multi_core(ctx, client_proc, serv_proc->info.core);
  // }
}

static void ewok_sys_ipc_end(context_t* ctx) {
  log_debug("ewok_sys_ipc_end not impl\n");

  // proc_t* serv_proc = get_current_proc();
  // ipc_task_t* ipc = proc_ipc_get_task(serv_proc);
  // if (serv_proc == NULL || serv_proc->space->ipc_server.entry == 0 ||
  //     ipc == NULL)
  //   return;

  // proc_restore_state(ctx, serv_proc,
  // &serv_proc->space->ipc_server.saved_state); if (serv_proc->info.state ==
  // READY) proc_ready(serv_proc);

  // // wake up request proc to get return
  // proc_ipc_close(serv_proc, ipc);
  // proc_wakeup(serv_proc->info.pid, (uint32_t)&serv_proc->space->ipc_server);

  // if (proc_ipc_fetch(serv_proc) != 0)  // fetch next buffered ipc
  //   proc_ipc_do_task(ctx, serv_proc, serv_proc->info.core);
  // else
  //   schedule(ctx);
}

static int32_t ewok_sys_ipc_disable(void) {
  log_debug("ewok_sys_ipc_disable not impl\n");

  // proc_t* cproc = proc_get_proc(get_current_proc());
  // ipc_task_t* ipc = proc_ipc_get_task(cproc);
  // if (ipc != NULL && ipc->state != IPC_IDLE) return -1;
  // cproc->space->ipc_server.disabled = true;
  return 0;
}

static void ewok_sys_ipc_enable(void) {
  log_debug("ewok_sys_ipc_enable not impl\n");

  // proc_t* cproc = proc_get_proc(get_current_proc());
  // if (!cproc->space->ipc_server.disabled) return;

  // cproc->space->ipc_server.disabled = false;
  // proc_wakeup(cproc->info.pid, (uint32_t)&cproc->space->ipc_server);
}

static int32_t ewok_sys_proc_ping(int32_t pid) {
  log_debug("ewok_sys_proc_ping not impl\n");

  // proc_t* proc = proc_get(pid);
  // if (proc == NULL || !proc->space->ready_ping) return -1;
  return 0;
}

static void ewok_sys_proc_ready_ping(void) {
  log_debug("ewok_sys_proc_ready_ping not impl\n");

  // proc_t* cproc = get_current_proc();
  // cproc->space->ready_ping = true;
}

static kevent_t* ewok_sys_get_kevent_raw(void) {
  log_debug("ewok_sys_get_kevent_raw not impl\n");

  // proc_t* cproc = get_current_proc();
  // if (cproc->info.pid != _core_proc_pid)  // only core proc access allowed.
  //   return NULL;

  // kevent_t* kev = kev_pop();
  // if (kev == NULL) {
  //   return NULL;
  // }

  // kevent_t* ret = (kevent_t*)proc_malloc(cproc, sizeof(kevent_t));
  // ret->type = kev->type;
  // ret->data[0] = kev->data[0];
  // ret->data[1] = kev->data[1];
  // ret->data[2] = kev->data[2];
  // kfree(kev);
  // return ret;
}

static void ewok_sys_get_kevent(context_t* ctx) {
  log_debug("ewok_sys_get_kevent not impl\n");

  // ctx->gpr[0] = 0;
  // kevent_t* kev = sys_get_kevent_raw();
  // if (kev == NULL) {
  //   proc_block_on(-1, (uint32_t)kev_init);
  //   schedule(ctx);
  //   return;
  // }
  // ctx->gpr[0] = (int32_t)kev;
}

static void ewok_sys_proc_block(context_t* ctx, int32_t pid, uint32_t evt) {
  log_debug("ewok_sys_proc_block not impl\n");

  // proc_t* proc_by = proc_get_proc(proc_get(pid));
  // if (proc_by != NULL) {
  //   proc_block_on(proc_by->info.pid, evt);
  //   schedule(ctx);
  // }
}

static void ewok_sys_proc_wakeup(context_t* ctx, uint32_t evt) {
  log_debug("ewok_sys_proc_wakeup not impl\n");

  (void)ctx;
  // proc_t* proc = proc_get_proc(get_current_proc());
  // proc_wakeup(proc->info.pid, evt);
}

static void ewok_sys_core_proc_ready(void) {
  log_debug("ewok_sys_core_proc_ready not impl\n");

  // proc_t* cproc = get_current_proc();
  // if (cproc->info.owner > 0) return;
  _core_proc_ready = true;
  // _core_proc_pid = cproc->info.pid;
}

static int32_t ewok_sys_core_proc_pid(void) { return _core_proc_pid; }

static int32_t ewok_sys_get_kernel_tic(uint32_t* sec, uint32_t* hi,
                                       uint32_t* low) {
  log_debug("ewok_sys_get_kernel_tic not impl\n");

  // if (sec != NULL) *sec = _kernel_sec;
  // if (hi != NULL) *hi = _kernel_usec >> 32;
  // if (low != NULL) *low = _kernel_usec & 0xffffffff;
  return 0;
}

static int32_t ewok_sys_interrupt_setup(uint32_t interrupt, uint32_t entry,
                                        uint32_t data) {
  log_debug("ewok_sys_interrupt_setup not impl\n");

  // proc_t* cproc = get_current_proc();
  // if (cproc->info.owner > 0) return -1;
  // return interrupt_setup(cproc, interrupt, entry, data);
  return 0;
}

static void ewok_sys_interrupt_end(context_t* ctx) {
  log_debug("ewok_sys_interrupt_end not impl\n");

  // interrupt_end(ctx);
}

static inline void ewok_sys_safe_set(context_t* ctx, int32_t* to, int32_t v) {
  log_debug("ewok_sys_safe_set not impl\n");

  // ctx->gpr[0] = -1;
  // proc_t* proc = proc_get_proc(get_current_proc());
  // if (*to != 0 && v != 0) {
  //   proc_block_on(proc->info.pid, (uint32_t)to);
  //   schedule(ctx);
  //   return;
  // }

  // *to = v;
  // ctx->gpr[0] = 0;
  // proc_wakeup(proc->info.pid, (uint32_t)to);
}

static inline void ewok_sys_soft_int(context_t* ctx, int32_t to_pid,
                                     uint32_t entry, uint32_t data) {
  log_debug("ewok_sys_soft_int not impl\n");

  // proc_t* proc = proc_get_proc(get_current_proc());
  // if (proc->info.owner > 0) return;
  // interrupt_soft_send(ctx, to_pid, entry, data);
}

static inline int32_t ewok_sys_proc_uuid(int32_t pid) {
  log_debug("ewok_sys_proc_uuid not impl\n");
  return 0;
}

static inline void ewok_sys_root(void) {
#ifdef KCONSOLE
  kconsole_close();
#endif
}

void ewok_svc_handler(int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg4,
                      int32_t arg5, ...) {
  thread_t* current = thread_current();
  interrupt_context_t* context = current->ctx->ic;
  ewok_context_t* ctx = context;

  int code = context_fn(context);

  _svc_total++;
  _svc_counter[code]++;

  switch (code) {
    case EWOK_SYS_EXIT:
      ewok_sys_exit(ctx, arg0);
      return;
    case EWOK_SYS_SIGNAL_SETUP:
      ewok_sys_signal_setup(arg0);
      return;
    case EWOK_SYS_SIGNAL:
      ewok_sys_signal(ctx, arg0, arg1);
      return;
    case EWOK_SYS_SIGNAL_END:
      ewok_sys_signal_end(ctx);
      return;
    case EWOK_SYS_MALLOC:
      ctx->gpr[0] = ewok_sys_malloc(arg0);
      return;
    case EWOK_SYS_REALLOC:
      ctx->gpr[0] = ewok_sys_realloc((void*)arg0, arg1);
      return;
    case EWOK_SYS_FREE:
      ewok_sys_free(arg0);
      return;
    case EWOK_SYS_GET_PID:
      ctx->gpr[0] = ewok_sys_getpid(arg0);
      return;
    case EWOK_SYS_GET_THREAD_ID:
      ctx->gpr[0] = ewok_sys_get_thread_id();
      return;
    case EWOK_SYS_USLEEP:
      ewok_sys_usleep(ctx, (uint32_t)arg0);
      return;
    case EWOK_SYS_EXEC_ELF:
      ewok_sys_load_elf(ctx, (const char*)arg0, (void*)arg1, (uint32_t)arg2);
      return;
    case EWOK_SYS_FORK:
      ewok_sys_fork(ctx);
      return;
    case EWOK_SYS_DETACH:
      ewok_sys_detach();
      return;
    case EWOK_SYS_WAIT_PID:
      ewok_sys_waitpid(ctx, arg0);
      return;
    case EWOK_SYS_YIELD:
      schedule(ctx);
      return;
    case EWOK_SYS_PROC_SET_UID:
      //   ctx->gpr[0] = ewok_sys_proc_set_uid(arg0);
      return;
    case EWOK_SYS_PROC_GET_UID:
      //   ctx->gpr[0] = get_current_proc()->info.owner;
      return;
    case EWOK_SYS_PROC_GET_CMD:
      ctx->gpr[0] = ewok_sys_proc_get_cmd(arg0, (char*)arg1, arg2);
      return;
    case EWOK_SYS_PROC_SET_CMD:
      ewok_sys_proc_set_cmd((const char*)arg0);
      return;
    case EWOK_SYS_GET_SYS_INFO:
      ewok_sys_get_sys_info((sys_info_t*)arg0);
      return;
    case EWOK_SYS_GET_SYS_STATE:
      ewok_sys_get_sys_state((sys_state_t*)arg0);
      return;
    case EWOK_SYS_GET_KERNEL_TIC:
      ctx->gpr[0] = ewok_sys_get_kernel_tic((uint32_t*)arg0, (uint32_t*)arg1,
                                            (uint32_t*)arg2);
      return;
    case EWOK_SYS_GET_PROCS:
      ctx->gpr[0] = (int32_t)get_procs((int32_t*)arg0);
      return;
    case EWOK_SYS_PROC_SHM_ALLOC:
      ctx->gpr[0] = ewok_sys_shm_alloc(arg0, arg1);
      return;
    case EWOK_SYS_PROC_SHM_MAP:
      ctx->gpr[0] = (int32_t)ewok_sys_shm_map(arg0);
      return;
    case EWOK_SYS_PROC_SHM_UNMAP:
      ctx->gpr[0] = ewok_sys_shm_unmap(arg0);
      return;
    case EWOK_SYS_PROC_SHM_REF:
      ctx->gpr[0] = ewok_sys_shm_ref(arg0);
      return;
    case EWOK_SYS_THREAD:
      ewok_sys_thread(ctx, (uint32_t)arg0, (uint32_t)arg1, arg2);
      return;
    case EWOK_SYS_KPRINT:
      ewok_sys_kprint((const char*)arg0, arg1);
      return;
    case EWOK_SYS_MEM_MAP:
      ctx->gpr[0] =
          ewok_sys_mem_map((uint32_t)arg0, (uint32_t)arg1, (uint32_t)arg2);
      return;
    case EWOK_SYS_IPC_SETUP:
      ewok_sys_ipc_setup(ctx, arg0, arg1, arg2);
      return;
    case EWOK_SYS_IPC_CALL:
      ewok_sys_ipc_call(ctx, arg0, arg1, (proto_t*)arg2);
      return;
    case EWOK_SYS_IPC_GET_RETURN:
      ewok_sys_ipc_get_return(ctx, arg0, (uint32_t)arg1, (proto_t*)arg2);
      return;
    case EWOK_SYS_IPC_SET_RETURN:
      ewok_sys_ipc_set_return(ctx, (uint32_t)arg0, (proto_t*)arg1);
      return;
    case EWOK_SYS_IPC_END:
      ewok_sys_ipc_end(ctx);
      return;
    case EWOK_SYS_IPC_GET_ARG:
      ctx->gpr[0] =
          ewok_sys_ipc_get_info((uint32_t)arg0, (int32_t*)arg1, (proto_t*)arg2);
      return;
    case EWOK_SYS_IPC_PING:
      ctx->gpr[0] = ewok_sys_proc_ping(arg0);
      return;
    case EWOK_SYS_IPC_READY:
      ewok_sys_proc_ready_ping();
      return;
    case EWOK_SYS_GET_KEVENT:
      ewok_sys_get_kevent(ctx);
      return;
    case EWOK_SYS_WAKEUP:
      ewok_sys_proc_wakeup(ctx, arg0);
      return;
    case EWOK_SYS_BLOCK:
      ewok_sys_proc_block(ctx, arg0, arg1);
      return;
    case EWOK_SYS_CORE_READY:
      ewok_sys_core_proc_ready();
      return;
    case EWOK_SYS_CORE_PID:
      ctx->gpr[0] = ewok_sys_core_proc_pid();
      return;
    case EWOK_SYS_IPC_DISABLE:
      ctx->gpr[0] = ewok_sys_ipc_disable();
      return;
    case EWOK_SYS_IPC_ENABLE:
      ewok_sys_ipc_enable();
      return;
    case EWOK_SYS_INTR_SETUP:
      ctx->gpr[0] = ewok_sys_interrupt_setup((uint32_t)arg0, (uint32_t)arg1,
                                             (uint32_t)arg2);
      return;
    case EWOK_SYS_INTR_END:
      ewok_sys_interrupt_end(ctx);
      return;
    case EWOK_SYS_SAFE_SET:
      ewok_sys_safe_set(ctx, (int32_t*)arg0, arg1);
      return;
    case EWOK_SYS_SOFT_INT:
      ewok_sys_soft_int(ctx, arg0, arg1, arg2);
      return;
    case EWOK_SYS_PROC_UUID:
      ctx->gpr[0] = ewok_sys_proc_uuid(arg0);
      return;
    case EWOK_SYS_V2P:
      ctx->gpr[0] = V2P(arg0);
      return;
    case EWOK_SYS_P2V:
      ctx->gpr[0] = P2V(arg0);
      return;
    case EWOK_SYS_CLOSE_KCONSOLE:
      ewok_sys_root();
      return;
  }
}