/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "thread.h"

#include "fd.h"
#include "preempt.h"
#include "loader.h"
#include "schedule.h"
#include "syscall.h"

thread_t* current_threads[MAX_CPU] = {0};
thread_t* schedulable_head_thread[MAX_CPU] = {0};
thread_t* schedulable_tail_thread[MAX_CPU] = {0};

thread_t* recycle_head_thread = NULL;
thread_t* recycle_tail_thread = NULL;
u32 recycle_head_thread_count = 0;

u32 thread_ids = 0;
lock_t thread_lock;
#if MP_ENABLE
static volatile int thread_create_lock = 0;
#endif

#define log_debug
// #define DEBUG 1

void thread_init() {  }

thread_t* thread_create_level(void* entry, void* data, u32 level) {
  thread_t* thread =
      thread_create_ex(entry, KERNEL_THREAD_STACK_SIZE, THREAD_STACK_SIZE, data,
                       level, THREAD_NEW);
  return thread;
}

thread_t* thread_create_name(char* name, void* entry, void* data) {
  thread_t* t = thread_create(entry, data);
  if (t == NULL) return t;
  t->name = name;
  return t;
}

thread_t* thread_create_name_level(char* name, void* entry, void* data,
                                   u32 level) {
  thread_t* t = thread_create_level(entry, data, level);
  if (t == NULL) return t;
  t->name = name;
  return t;
}

thread_t* thread_create(void* entry, void* data) {
  return thread_create_level(entry, data, LEVEL_USER);
}

thread_t* thread_create_ex_name(char* name, void* entry, u32 size, void* data,
                                u32 level, u32 flags) {
  thread_t* t = thread_create_ex(entry, KERNEL_THREAD_STACK_SIZE, size, data,
                                 level, flags);
  if (t == NULL) return t;
  char* kname = kmalloc(kstrlen(name) + 1, KERNEL_TYPE);
  kstrcpy(kname, name);
  t->name = kname;
  return t;
}

void thread_init_default(thread_t* thread, u32 level, u32* entry, void* data) {
  thread->id = thread_ids++;
  thread->lock = 0;
  thread->next = NULL;
  thread->rt_wait_next = NULL;
  thread->priority = 1;
  thread->priority_base = 1;
  thread->counter = 0;
  thread->sleep_counter = 0;
  thread->state = THREAD_CREATE;
  thread->level = level;
  thread->exec = NULL;
  thread->cpu_id = cpu_get_id();
  thread->fd_size = 40;
  thread->fd_number = 0;
  thread->data = data;
  thread->mem = 0;
  thread->ticks = 0;
  thread->clear_child_tid = NULL;
  thread->user_tp = NULL;
}

thread_t* thread_create_ex(void* entry, u32 kstack_size, u32 ustack_size,
                           void* data, u32 level, u32 flags) {
  if (ustack_size <= 0) {
    log_error("thread create ex  user stack size is 0\n");
    return NULL;
  }
#if MP_ENABLE
  while (__sync_lock_test_and_set(&thread_create_lock, 1)) {}
#endif

  thread_t* thread = kmalloc(sizeof(thread_t), KERNEL_TYPE);
  thread_init_default(thread, level, entry, data);

  thread->fds = kmalloc(sizeof(fd_t*) * thread->fd_size, KERNEL_TYPE);
  if (thread->fds != NULL) {
    kmemset(thread->fds, 0, sizeof(fd_t*) * thread->fd_size);
  }

  // context init
  context_t* ctx = kmalloc(sizeof(context_t), KERNEL_TYPE);
  thread->ctx = ctx;
  /* 必须清零：ctx->ic 只在 context_switch() 里赋值，若这里留未初始化值，
   * schedule_switch() 会把它当栈指针用（interrupt_exit_context → ldr sp, ic），
   * 于是 SVC bank sp 变成垃圾，该线程下一次 SVC 会在 svc_handler 第一条指令
   * （stmdb sp,{r0-r12,sp,lr}^）炸掉，dfar = sp - 0x3C。
   * thread_copy() 里是清过的，这里漏了。 */
  kmemset(ctx, 0, sizeof(context_t));
  ctx->tid = thread->id;

  void* ksp = kmalloc(kstack_size, KERNEL_TYPE);
  void* usp = kmalloc_alignment(ustack_size, PAGE_SIZE, KERNEL_TYPE);
  ctx->ksp_start = (u64)ksp;
  ctx->ksp_end = (u64)ksp + kstack_size;
  ctx->ksp_size = kstack_size;
  ctx->usp = (u64)usp + ustack_size;
  ctx->usp_size = ustack_size;

  u32 mode = USER_MODE;
  if (level == LEVEL_KERNEL || level == LEVEL_KERNEL_SHARE) {
    mode = KERNEL_MODE;
  }

#ifdef VM_ENABLE
  // vm init
  vmemory_t* vm = kmalloc(sizeof(vmemory_t), KERNEL_TYPE);
  thread->vm = vm;
  vm->tid = thread->id;
  vmemory_init(vm, level, (vaddr_t)usp, ustack_size, flags);

  vmemory_area_t* vm_stack = vmemory_area_find_flag(vm->vma, MEMORY_STACK);
  context_init(ctx, ctx->ksp_end, vm_stack->vend, (u64)entry, mode, thread->cpu_id);
#else
  context_init(ctx, ctx->ksp_end, ctx->usp, (u64)entry, mode, thread->cpu_id);
#endif

  // vfs
  thread->vfs = kmalloc(sizeof(vfs_t), KERNEL_TYPE);
  if (thread->vfs != NULL) {
    kmemset(thread->vfs, 0, sizeof(vfs_t));
    thread->vfs->root = vfs_find(NULL, "/");
    thread->vfs->pwd = thread->vfs->root;
    thread->vfs->users = 1;
  }
  // file description
  thread_fill_fd(thread);

  // check thread data
  int ret = thread_check(thread);
#if MP_ENABLE
  __sync_lock_release(&thread_create_lock);
#endif
  return thread;
}

thread_t* thread_copy(thread_t* thread, u32 flags) {
  if (thread == NULL) {
    return NULL;
  }
  log_debug("thread copy start\n");

  thread_t* copy = kmalloc(sizeof(thread_t), KERNEL_TYPE);
  kmemset(copy, 0, sizeof(thread_t));
  kmemmove(copy, thread, sizeof(thread_t));
  log_debug("thread init default\n");

  thread_init_default(copy, thread->level, thread->ctx->eip, thread->data);
  /* 【fork 子进程必须继承父进程的 TLS】thread_init_default 会把 user_tp 清成
   * NULL（新线程本应如此）。但 fork 出来的子进程要沿用它自己的 TLS 基址：
   * musl 的 __pthread_self()/errno 依赖它，清零后 __pthread_self() 变成负地址
   * （实测 config 在 _Fork 里 `str self->...` 崩在 dfar=0xffffffa0）。
   * 所以这两个复制必须放在 thread_init_default 之后。 */
  copy->tinfo = thread->tinfo;
  copy->user_tp = thread->user_tp;
  copy->data = thread->data;
  copy->pid = thread->id;
  // copy->name = kmalloc(kstrlen(thread->name), KERNEL_TYPE);
  // kstrcpy(copy->name, thread->name);
  copy->name = thread->name;
  copy->exec = NULL;
  /* 调度按 counter 最低优先；继承父进程 counter 会让新进程长期饿死。
   * 新线程从 0 起跑，跑一会儿自然与其它线程汇合。 */
  copy->counter = 0;
  copy->fault_count = 0;
  copy->sleep_counter = 0;
  copy->dump_count = 0;
  copy->ticks = 0;

  // context init
  context_t* ctx = kmalloc(sizeof(context_t), KERNEL_TYPE);
  kmemset(ctx, 0, sizeof(context_t));

  copy->ctx = ctx;
  ctx->tid = copy->id;

  u64 kstack_size = thread->ctx->ksp_size;
  u64 ustack_size = thread->ctx->usp_size;

  u64 ksp = (u64)kmalloc(kstack_size, KERNEL_TYPE);
  ctx->ksp_start = ksp;
  ctx->ksp_end = ksp + kstack_size;
  ctx->ksp_size = kstack_size;
  ctx->usp = 0;
  ctx->usp_size = ustack_size;

  context_clone(copy->ctx, thread->ctx);
  context_inherit_live(copy->ctx, thread->ctx->ic);

  if (thread->vfs != NULL) {
    copy->vfs = thread->vfs;
    if (copy->vfs->root == NULL) {
      copy->vfs->root = vfs_find(NULL, "/");
    }
    if (copy->vfs->pwd == NULL) {
      copy->vfs->pwd = copy->vfs->root;
    }
    copy->vfs->users++;
  } else {
    copy->vfs = kmalloc(sizeof(vfs_t), KERNEL_TYPE);
    if (copy->vfs != NULL) {
      kmemset(copy->vfs, 0, sizeof(vfs_t));
      copy->vfs->root = vfs_find(NULL, "/");
      copy->vfs->pwd = copy->vfs->root;
      copy->vfs->users = 1;
    }
  }

#ifdef VM_ENABLE
  // vm init
  copy->vm = kmalloc(sizeof(vmemory_t), KERNEL_TYPE);
  copy->vm->tid = copy->id;

  // init vm include stack heap exec
  vmemory_clone(copy->vm, thread->vm, flags);
  if (copy->vm->upage == NULL) {
    log_error("thread copy: vm upage null after clone\n");
    return NULL;
  }
#endif

  // Always give the child its own fd table; optionally share fd_t objects.
  copy->fd_size = thread->fd_size;
  copy->fd_number = 0;
  copy->fds = kmalloc(sizeof(fd_t*) * copy->fd_size, KERNEL_TYPE);
  if (copy->fds == NULL) {
    log_error("thread copy fd alloc failed\n");
    return NULL;
  }
  kmemset(copy->fds, 0, sizeof(fd_t*) * copy->fd_size);
  if (flags & FS_CLONE) {
    copy->fd_number = thread->fd_number;
    for (int i = 0; i < (int)thread->fd_number; i++) {
      copy->fds[i] = thread->fds[i];
      if (copy->fds[i] != NULL) {
        copy->fds[i]->use_count++;
      }
    }
  } else {
    thread_fill_fd(copy);
  }

  // check thread data
  int ret = thread_check(copy);
  log_debug("thread copy end\n");
  return copy;
}

int thread_check(thread_t* thread) {
  if (thread->ctx->ksp_start == NULL) {
    log_error("create thread %d faild for ksp start is null\n", thread->id);
    return -1;
  }
  if (thread->ctx->ksp_end == NULL) {
    log_error("create thread %d faild for ksp end is null\n", thread->id);
    return -1;
  }
  if (thread->ctx->ksp_size <= 0) {
    log_error("create thread %d faild for ksp size is 0\n", thread->id);
    return -1;
  }
  if ((thread->ctx->ksp_end - thread->ctx->ksp_start) !=
      thread->ctx->ksp_size) {
    log_error("create thread %d faild for ksp size is not equal\n", thread->id);
    return -1;
  }

  if (thread->ctx->usp_size <= 0) {
    log_error("create thread %d faild for ustack size is 0\n", thread->id);
    return -1;
  }
  if (thread->ctx->usp <= 0) {
    log_error("create thread %d faild for ustack is 0\n", thread->id);
    return -1;
  }
#ifdef VM_ENABLE
  if (thread->vm->kpage == NULL) {
    log_error("create thread %d faild for kpage is null\n", thread->id);
    return -1;
  }
  if (thread->vm->upage == NULL) {
    log_error("create thread %d faild for upage is null\n", thread->id);
    return -1;
  }

  if (thread->vm->vma == NULL) {
    log_error("create thread %d faild for vma is null\n", thread->id);
    return -1;
  }

  // check stack map
  vmemory_area_t* vm_stack =
      vmemory_area_find_flag(thread->vm->vma, MEMORY_STACK);
  if (vm_stack == NULL) {
    log_error("create thread %d faild for stack is null\n", thread->id);
    return -1;
  }

  if (thread->ctx->usp < vm_stack->vaddr || thread->ctx->usp > vm_stack->vend) {
    log_error("create thread %d faild for ustack %lx range [%lx - %lx] error\n",
              thread->id, thread->ctx->usp, vm_stack->vaddr, vm_stack->vend);
    return -1;
  }

  void* phy = page_v2p((u64*)thread->vm->upage, (void*)vm_stack->alloc_addr);
  if (phy == NULL) {
    log_error("thread map have error\n");
    return -1;
  }
#ifdef DEBUG
  vmemory_dump(thread->vm);
  thread_dump(thread, 0);
#endif
  log_debug("tid %d kpage %x upage %x\n", thread->id, thread->vm->kpage,
            thread->vm->upage);
#endif
  log_debug("tid %d ksp %x usp %x\n", thread->id, thread->ctx->ksp,
            thread->ctx->usp);

  return 0;
}

void thread_fill_fd(thread_t* thread) {
  if (thread == NULL || thread->fds == NULL) {
    return;
  }
  fd_ensure_stdio();
  thread->fds[STDIN] = fd_find(STDIN);
  thread->fds[STDOUT] = fd_find(STDOUT);
  thread->fds[STDERR] = fd_find(STDERR);
  thread->fd_number = 0;
  for (int i = STDIN; i <= STDERR; i++) {
    if (thread->fds[i] != NULL) {
      thread->fds[i]->use_count++;
      thread->fd_number = i + 1;
    }
  }
}

void thread_ensure_stdio(thread_t* thread) {
  if (thread == NULL || thread->fds == NULL) {
    return;
  }
  fd_ensure_stdio();
  for (int i = STDIN; i <= STDERR; i++) {
    if (thread->fds[i] == NULL) {
      fd_t* stdfd = fd_find(i);
      if (stdfd != NULL) {
        thread->fds[i] = stdfd;
        stdfd->use_count++;
      }
    }
    if (thread->fds[i] != NULL && i + 1 > (int)thread->fd_number) {
      thread->fd_number = i + 1;
    }
  }
}

void thread_exec_reset_fds(thread_t* thread) {
  if (thread == NULL || thread->fds == NULL) {
    return;
  }
  for (u32 i = 0; i < thread->fd_number; i++) {
    if (i <= STDERR) {
      continue;
    }
    if (thread->fds[i] != NULL) {
      fd_t* f = thread->fds[i];
      thread->fds[i] = NULL;
      fd_close(f);
    }
  }
  thread_ensure_stdio(thread);
}

void thread_sleep(thread_t* thread, u32 count) {
  thread->state = THREAD_SLEEP;
  if (count > 0) {
    /* YiYiYa: 【睡眠债不能累加】——KISS 设计把"真正换出"推迟到下一次 tick，
     * 线程在被换出前会继续跑很多帧、每帧一次 nanosleep ⇒ 原来的 `+=` 会把
     * 这些请求累加：miyoo 实测每 tick 间跑 ~13 帧、SDL_Delay(50) 每帧一次
     * ⇒ 债 ≈ 650 tick ⇒ 每次被换出要"睡" ≈659ms ✗（帧率被压到 13fps、
     * 按键 1~2 秒才有反应 ✗）。取 max：一个调度间隔内只保留最大的一次请求，
     * 每轮最多睡 requested 时长 ⇒ 仍被节流，但不会再出现 659ms 大冻 ✓。 */
    if (thread->sleep_counter < count) {
      thread->sleep_counter = count;
    }
    thread->counter += count;
  }
}

void thread_wait(thread_t* thread) {
#ifdef DEBUG_THREAD
  log_debug("thread %d wait==============> %d\n", current_threads[cpu_id]->id,
            thread->id);
#endif
  thread->state = THREAD_WAITING;
  schedule_switch();
}

void thread_wake(thread_t* thread) {
#ifdef DEBUG_THREAD
  log_debug("thread %d wake==============> %d\n", current_threads[cpu_id]->id,
            thread->id);
#endif
  thread->state = THREAD_RUNNING;
  thread->sleep_counter = 0;
}

void thread_set_entry(thread_t* thread, void* entry) {
  if (thread == NULL) return;
  interrupt_context_t* ic = thread->ctx->ksp;
  if (ic == NULL) {
    log_error("context is null cannot set ret\n");
    return;
  }
  context_set_entry(ic, entry);
}

void thread_set_arg(thread_t* thread, void* arg) {
  if (thread == NULL) return;
  interrupt_context_t* ic = thread->ctx->ksp;
  if (ic == NULL) {
    log_error("context is null cannot set ret\n");
    return;
  }
  context_ret(ic) = arg;
}

void thread_reset_user_context(thread_t* thread, void* entry, void* stack_top) {
  if (thread == NULL || thread->ctx == NULL || thread->ctx->ksp_end == 0) {
    log_error("thread reset user context invalid thread\n");
    return;
  }
  context_t* ctx = thread->ctx;
#if defined(ARM64)
  context_init(ctx, ctx->ksp_end, (u64)stack_top, (u64)entry, LEVEL_USER,
               thread->cpu_id);
#else
  context_init(ctx, (u32*)ctx->ksp_end, (u32*)stack_top, (u32*)entry, LEVEL_USER,
               thread->cpu_id);
#endif
  interrupt_context_t* ic = context_exec_live(ctx);
  if (ic == NULL) {
    return;
  }
  /* musl _start: first arg = stack pointer (argc/argv/auxv on stack) */
#if defined(ARM64)
  context_arg0(ic) = (u64)stack_top;
  context_arg1(ic) = 0;
  context_arg2(ic) = 0;
  context_arg3(ic) = 0;
#else
  context_arg0(ic) = (u32)stack_top;
  context_arg1(ic) = 0;
  context_arg2(ic) = 0;
  context_arg3(ic) = 0;
#endif
}

void thread_set_ret(thread_t* thread, u32 ret) {
  if (thread == NULL) return;
  interrupt_context_t* ic = thread->ctx->ic;
  if (ic == NULL) {
    ic = thread->ctx->ksp;
  }
  if (ic == NULL) {
    log_error("context is null cannot set ret\n");
    return;
  }
  context_ret(ic) = ret;
}

void thread_set_params(thread_t* thread, void* args, int size) {
  if (thread == NULL) return;
  // todo copy
}

void thread_reset_user_stack(thread_t* thread, u32* ustack) {
  // todo
}

static int thread_dequeue(thread_t* thread, int cpu) {
  thread_t* prev = NULL;
  thread_t* v = schedulable_head_thread[cpu];
  while (v != NULL) {
    if (v == thread) {
      if (prev == NULL) {
        schedulable_head_thread[cpu] = v->next;
      } else {
        prev->next = v->next;
      }
      if (schedulable_tail_thread[cpu] == thread) {
        schedulable_tail_thread[cpu] = prev;
      }
      v->next = NULL;
      return 1;
    }
    prev = v;
    v = v->next;
  }
  return 0;
}

static void thread_enqueue(thread_t* thread, int cpu) {
  thread->next = NULL;
  if (schedulable_head_thread[cpu] == NULL) {
    schedulable_head_thread[cpu] = thread;
    schedulable_tail_thread[cpu] = thread;
  } else {
    schedulable_tail_thread[cpu]->next = thread;
    schedulable_tail_thread[cpu] = thread;
  }
  if (current_threads[cpu] == NULL) {
    current_threads[cpu] = thread;
  }
}

void thread_bind_cpu(thread_t* thread, int cpu) {
  if (thread == NULL || cpu < 0 || cpu >= MAX_CPU) return;
  int old = (int)thread->cpu_id;
  if (old == cpu) return;

  int on_queue = thread->state == THREAD_RUNNING ||
                 thread->state == THREAD_RUNABLE ||
                 thread->state == THREAD_WAITING ||
                 thread->state == THREAD_SLEEP;
  /* 还没进队列，或者正在那颗核上跑：只改 cpu_id。
   * 正在跑的核要等下一次调度把上下文存进线程，再由 thread_bind_finish 挪队列。
   * 此刻就摘走的话，两颗核会同时用同一份没保存的上下文。 */
  if (!on_queue || old < 0 || old >= MAX_CPU ||
      current_threads[old] == thread) {
    thread->cpu_id = (u32)cpu;
    return;
  }
  if (!thread_dequeue(thread, old)) {
    thread->cpu_id = (u32)cpu;
    return;
  }
  thread->cpu_id = (u32)cpu;
  thread_enqueue(thread, cpu);
}

void thread_bind_finish(thread_t* thread, int from_cpu) {
  if (thread == NULL || from_cpu < 0 || from_cpu >= MAX_CPU) return;
  int dest = (int)thread->cpu_id;
  if (dest == from_cpu || dest < 0 || dest >= MAX_CPU) return;
  if (!thread_dequeue(thread, from_cpu)) return;
  thread_enqueue(thread, dest);
}

void thread_add(thread_t* thread) {
  // lock_acquire(&thread_lock);

  // 挂到 thread_bind_cpu 指定的核；没绑过就是创建时的当前核。
  int cpu_id = (int)thread->cpu_id;
  if (cpu_id >= MAX_CPU) {
    cpu_id = cpu_get_id();
  }
  if (schedulable_head_thread[cpu_id] == NULL) {
    schedulable_head_thread[cpu_id] = thread;
    schedulable_tail_thread[cpu_id] = thread;
  } else {
    schedulable_tail_thread[cpu_id]->next = thread;
    schedulable_tail_thread[cpu_id] = thread;
  }
  thread->state = THREAD_RUNABLE;
  if (current_threads[cpu_id] == NULL) {
    if (schedulable_head_thread[cpu_id] == NULL) {
      log_error("no thread please create a thread\n");
      cpu_halt();
    }
    current_threads[cpu_id] = schedulable_head_thread[cpu_id];
    // current_context = &current_threads[cpu_id]->context;
  }
  // lock_release(&thread_lock);
}

void thread_remove(thread_t* thread) {
  // lock_acquire(&thread_lock);
  int cpu_id = cpu_get_id();
  thread->state = THREAD_STOPPED;
  thread->counter += 1000;

  /* 【只摘除该节点，绝不能清空整条链】原实现：若 thread 恰好是
   * schedulable_head_thread[cpu_id]，直接 head=tail=NULL —— 该核调度链上
   * 的其它线程（比如 shell）全部从调度器里消失，线程再也不被调度
   * （raspi3 实测：/bin/config 退出时恰为链表头，cpu0 的 shell 从此
   * 无法运行，表现为"输入几个字符后系统卡死"）。 */
  thread_t* prev = NULL;
  thread_t* v = schedulable_head_thread[cpu_id];
  while (v != NULL) {
    if (v == thread) {
      if (prev == NULL) {
        schedulable_head_thread[cpu_id] = v->next; /* 是头：头指向下一个 */
      } else {
        prev->next = v->next;
      }
      if (schedulable_tail_thread[cpu_id] == thread) {
        schedulable_tail_thread[cpu_id] = prev; /* prev 可能为 NULL → 空链 */
      }
      v->next = NULL;
      // lock_release(&thread_lock);
      return;
    }
    prev = v;
    v = v->next;
  }
  // lock_release(&thread_lock);
}

void thread_destroy(thread_t* thread) {
  if (thread == NULL) return;
  if (thread->ctx->ksp != NULL) {
    // kfree(thread->ctx->ksp);
  }
  if (thread->ctx->usp != NULL) {
    // kfree(thread->ctx->usp);
  }
  kfree(thread);
}

void thread_recycle(thread_t* thread) {
  thread_remove(thread);
  // add into cycle thread
  if (recycle_head_thread == NULL) {
    recycle_head_thread = thread;
    recycle_tail_thread = thread;
  } else {
    recycle_tail_thread->next = thread;
    recycle_tail_thread = thread;
  }
  recycle_head_thread_count++;
  /* 【不要在这里 free 页表】原 todo 是 page_free(thread->vm->upage, ...)，
   * 但此刻还在死线程自己的上下文里：它的页表可能仍是 TTBR0 活跃表。
   * 页表（L2/L1）的归还已由 thread_recycle_process → vmemory_destroy 完成
   * （vma.c），用户页 + L2 表 + L1 表都在那儿统一释放。 */
}

void thread_stop(thread_t* thread) {
  if (thread == NULL) return;
  thread->state = THREAD_STOPPED;
  /* 【退出临界区不可抢占】thread_stop 后到真正切走（schedule_switch）之间，
   * 本线程仍站在自己的内核栈上。若此时被时钟换出，current_threads 立刻改人，
   * thread_recycle_process 会误判"它没在跑"而把它的 ctx->ksp_start 释放掉 ⇒
   * 切回来即 UAF。这里禁止抢占，切到新线程后由 preempt_reset() 清计数。 */
  preempt_disable();
  /* 【移入回收队列】不能在这里直接释放：线程可能还在自己的栈上跑
   * （thread_exit 就发生在它自己的上下文里）。真正的释放由调度器在切换到
   * 其它线程之后调用 thread_recycle_process() 完成。 */
  thread_recycle(thread);
}

/* 【延迟回收】遍历 recycle 队列，释放已退出线程的地址空间与内存。
 * 只回收"不是当前线程"的：退出瞬间它可能还站在自己的内核栈上，
 * 留到下一轮（当前线程换人之后）再处理。
 * 页表（L2/L1）的归还也在 vmemory_destroy 里一并完成（每个死进程此前
 * 要漏 16KB L1 + n×1KB L2）。
 * 还不做的：fd 条目、内核区（0x82000000+）私有 L2 副本 —— 体量小，留待下一轮。 */
/* 【最后一个共享成员判定】vmemory_t.ref 注释写着 "free only when last ref
 * drops"，但全树只在 vmemory_clone 里 ++、从没有 -- ✗ —— pthread（CLONE_VM）
 * 共享的 upage/用户物理页永远不满足 ref<=1。回收线程时扫一遍所有"还可能
 * 活着"的线程（各核调度链 + 回收队列）：没有同 upage 的其它成员，就认为
 * 自己是最后一个，让调用处把 ref 归 1 交给 vmemory_destroy 真正释放。
 * 已回收线程的 vm 已置 NULL（自动跳过）；upage 指针复用导致的误判只会
 * "多保留"（泄漏但安全），不会提前释放 ✓。 */
static int thread_vm_shared_alive(thread_t* self) {
  if (self == NULL || self->vm == NULL || self->vm->upage == NULL) {
    return 0;
  }
  for (int i = 0; i < MAX_CPU; i++) {
    for (thread_t* p = schedulable_head_thread[i]; p != NULL; p = p->next) {
      if (p != self && p->vm != NULL && p->vm->upage != NULL &&
          p->vm->upage == self->vm->upage) {
        return 1;
      }
    }
  }
  for (thread_t* p = recycle_head_thread; p != NULL; p = p->next) {
    if (p != self && p->vm != NULL && p->vm->upage != NULL &&
        p->vm->upage == self->vm->upage) {
      return 1;
    }
  }
  return 0;
}

void thread_recycle_process(void) {
  thread_t* cur = thread_current();
  thread_t* v = recycle_head_thread;
  thread_t* prev = NULL;
  while (v != NULL) {
    thread_t* next = v->next;
    /* 【多核保护】v != cur 只保护了"本核"的当前线程。退出的线程要等
     * 它自己所在核的下一次调度才真正切走，在那之前它还站在自己的内核
     * 栈上执行。若此时别的核的 recycle_process 把它释放（栈/ctx/thread
     * 全部 kfree），僵尸线程就在已释放的内存上继续跑，破坏新分配的内存
     * （raspi3 实测：/bin/config 退出即回收，shell 的下一次输入卡死）。
     * 所以必须检查所有核的 current，仍在任一核上跑的线程留到下一轮。 */
    int still_running = 0;
    for (int c = 0; c < MAX_CPU; c++) {
      if (current_threads[c] == v) {
        still_running = 1;
        break;
      }
    }
    /* 【僵尸等待被 waitpid 回收前不能释放】退出线程先进 recycle 队列；若父进程
     * 还在（thread_find_id 能找到），它要留在队列里当僵尸供 thread_find_zombie_child
     * 找到。回收时 sys_waitpid 把 pid 置 -1，下一轮这里才会真正释放。
     * 父进程已不在（孤儿）则直接释放，避免泄漏。
     * 【注意：不要试图把 vm/ctx 提前到这里释放】退出的线程此刻可能仍站在自己的
     * 内核栈上、其页表可能仍是 TTBR0 活跃表（见 thread_recycle 的注释）。实测
     * 提前 kfree 内核栈 ⇒ 下一次系统调用返回时中断帧被复用 ⇒ PC 变垃圾(UNDEF)；
     * 提前释放 vm 也有"活跃页表被回收"的风险。真正的"退出即释放"由【本函数下一次
     * 被调度器调用时】完成（那时它已切走），语义上已经足够早。 */
    if (!still_running && v->pid != (u32)-1 &&
        thread_find_id((int)v->pid) != NULL) {
      prev = v;
      v = next;
      continue;
    }
    if (!still_running) {
      if (prev == NULL) {
        recycle_head_thread = next;
      } else {
        prev->next = next;
      }
      if (recycle_tail_thread == v) {
        recycle_tail_thread = prev;
      }
      if (recycle_head_thread_count > 0) {
        recycle_head_thread_count--;
      }
      if (v->vm != NULL) {
        /* 【共享页表：最后一个成员才真正释放】pthread（CLONE_VM）共享的
         * upage/物理页不满足 ref<=1，vmemory_destroy 会直接 return ⇒
         * 每次 spawn 多线程程序（gnuboy/SDL 等）退出/被 kill 后漏一整份
         * 页表 + 用户物理页。这里判定"最后一个"后把 ref 归 1 交给
         * vmemory_destroy（page_destroy 归还物理页/页表 + vma 节点）✓。 */
        if (v->vm->upage != NULL && !thread_vm_shared_alive(v)) {
          v->vm->ref = 1;
        }
        vmemory_destroy(v->vm);
        kfree(v->vm);
        v->vm = NULL;
      }
      if (v->ctx != NULL) {
        if (v->ctx->ksp_start != 0) {
          kfree((void*)(u32)v->ctx->ksp_start);
        }
        kfree(v->ctx);
        v->ctx = NULL;
      }
      if (v->vfs != NULL) {
        /* 【vfs 是浅拷贝共享的】thread_copy 里 copy->vfs = thread->vfs 且
         * users++。这里必须按引用计数释放：>1 时只减计数，最后一个使用者
         * 才 kfree。否则第二个退出者会对同一 vfs 二次释放 —— 实测 raspi2
         * 上表现为 ya_free bad state（早前 init 直接崩死在 ya_free）。
         * 【二分结论】27e167f 整体回退会导致 raspi3 无法启动到 shell，
         * 本段是 27e167f 中唯一的行为修改，单独恢复验证。 */
        if (v->vfs->users > 1) {
          v->vfs->users--;
        } else {
          kfree(v->vfs);
        }
        v->vfs = NULL;
      }
      if (v->fds != NULL) {
        kfree(v->fds);
        v->fds = NULL;
      }
      kfree(v);
    } else {
      prev = v;
    }
    v = next;
  }
}

thread_t* thread_head() { return schedulable_head_thread[cpu_get_id()]; }

void thread_exit(thread_t* thread, int code) {
  if (thread == NULL) return;
  /* 只在退出线程自己的上下文里关 fd。fork 后父子各持一份 use_count，
   * 这里减到 0 才会 lwip_close，监听端口才能放开。回收阶段再关会卡在调度里。 */
  if (thread == thread_current() && thread->fds != NULL) {
    u32 i;
    for (i = 0; i < thread->fd_number; i++) {
      if (thread->fds[i] != NULL) {
        fd_t* f = thread->fds[i];
        thread->fds[i] = NULL;
        fd_close(f);
      }
    }
  }
  thread->code = code;
  u32 parent_id = thread->pid;
  thread_stop(thread);
  thread_t* parent = thread_find_id((int)parent_id);
  if (parent != NULL && parent->state == THREAD_WAITING) {
    thread_wake(parent);
  }
}

/* 【exit_group：结束【整个进程】】musl 的 exit()/_Exit() 走 SYS_exit_group。
 * 停掉与本线程同属一个进程（共享地址空间）的其它线程；本线程自身随后由
 * thread_exit 退出（唤醒父进程 waitpid）。同进程判定用【共享页表根 upage】：
 * sys_clone 的 pthread 路径（CLONE_VM 且非 VFORK）走 VM_SAME，vmemory_clone
 * 里 upage 直接共享；fork/vfork 的子进程是 page_clone 的新页表 ⇒ upage 不同，
 * 不会误伤 ✓。
 * 停法 = thread_stop（标 STOPPED + 摘出调度链 + 进回收队列），回收由调度器
 * 统一完成。已 STOPPED 的跳过：thread_recycle 无防重入，重复入队会损坏链表 ✗。
 * 遍历先存 next：thread_stop 会改链表。 */
void thread_exit_group(thread_t* thread, int code) {
  if (thread == NULL) {
    return;
  }
  /* 【不误伤调用者】sys_kill 场景下调用者是别的进程（通常不同 upage，
   * 本来就到不了这里）；但如果是"线程杀自己进程里的另一个线程"，调用者
   * 与目标同 upage —— 必须跳过它，否则在它自己的上下文里 thread_stop 自己
   * （标 STOPPED/摘链后继续执行）会留下状态错乱的活线程 ✗。 */
  thread_t* self = thread_current();
  if (thread->vm != NULL && thread->vm->upage != NULL) {
    for (int i = 0; i < MAX_CPU; i++) {
      thread_t* t = schedulable_head_thread[i];
      while (t != NULL) {
        thread_t* next = t->next;
        if (t != thread && t != self && t->state != THREAD_STOPPED &&
            t->vm != NULL && t->vm->upage == thread->vm->upage) {
          thread_stop(t);
        }
        t = next;
      }
    }
  }
  thread_exit(thread, code);
}

thread_t* thread_find_next(thread_t* thread) {
  thread_t* v = schedulable_head_thread[cpu_get_id()];

  for (; v; v = v->next) {
    if (v->next == thread) {
      return v;
    }
  }
  return NULL;
}

void thread_run(thread_t* thread) {
  if (thread == NULL) {
    /* 【健壮性】线程创建失败（例如堆耗尽）时不能直接解引用 NULL —— 原实现会
     * 静默走进异常，连日志都看不到，非常难查。 */
    log_error("thread_run: thread is NULL (create failed?)\n");
    return;
  }
  if (thread->state == THREAD_CREATE) {
    thread_add(thread);
    thread->state = THREAD_RUNNING;
  }
  if (thread->state == THREAD_RUNABLE) {
    thread->state = THREAD_RUNNING;
  } else if (thread->state == THREAD_STOPPED) {
    thread->state = THREAD_RUNNING;
  }
#if MM_DEBUG_PROBE
  log_error("thread_run: id=%x name=%s state=%d level=%d prio=%d\n", thread->id,
            thread->name ? (char*)thread->name : "(null)", thread->state,
            thread->level, thread->priority);
#endif
}

void thread_yield() {
  thread_t* current = thread_current();
  if (current == NULL) {
    return;
  }
  if (current->state == THREAD_RUNNING) {
    current->counter++;
    schedule_next();
  }
}

thread_t* thread_current() {
  // lock_acquire(&thread_lock);
  int cpu_id = cpu_get_id();
  thread_t* t = current_threads[cpu_id];
  // lock_release(&thread_lock);
  return t;
}

void thread_set_current(thread_t* thread) {
  // lock_acquire(&thread_lock);
  int cpu_id = cpu_get_id();
  current_threads[cpu_id] = thread;
  // lock_release(&thread_lock);
}

context_t* thread_current_context() {
  // lock_acquire(&thread_lock);
  int cpu_id = cpu_get_id();
  thread_t* t = current_threads[cpu_id];
  // lock_release(&thread_lock);
  if (t == NULL) {
    return NULL;
  }
  return t->ctx;
}

int thread_find_fd_name(thread_t* thread, u8* name) {
  if (thread->fd_number == 0) {
    thread_fill_fd(thread);
  }
  if (thread->fd_number > thread->fd_size) {
    log_error("thread find fd name limit %d > %d\n", thread->fd_number,
              thread->fd_size);
    return -1;
  }
  for (int i = 0; i < thread->fd_number; i++) {
    fd_t* fd = thread->fds[i];
    if (fd && kstrcmp(name, fd->name) == 0) {
      return i;
    }
  }
  return -1;
}

int thread_add_fd(thread_t* thread, fd_t* fd) {
  if (thread->fd_number > thread->fd_size) {
    log_error("thread add fd limit\n");
    return -1;
  }
  for (int i = 0; i < thread->fd_number; i++) {
    fd_t* find_fd = thread->fds[i];
    if (find_fd == NULL) {
      thread->fds[i] = fd;
      fd->use_count++;
      return i;
    }
  }
  thread->fds[thread->fd_number] = fd;
  fd->use_count++;
  return thread->fd_number++;
}

fd_t* thread_find_fd_id(thread_t* thread, u32 fd) {
  if (thread == NULL || thread->fds == NULL) {
    return NULL;
  }
  if (thread->fd_number > thread->fd_size) {
    log_error("thread find number limit %d\n", fd);
    return NULL;
  }
  if (fd >= thread->fd_size) {
    log_error("thread find fd limit %d >= size %d\n", fd, thread->fd_size);
    return NULL;
  }
  if (thread->fds[fd] == NULL) {
    if (fd <= STDERR) {
      thread_ensure_stdio(thread);
    }
    if (thread->fds[fd] == NULL) {
      return NULL;
    }
  }
  if (fd + 1 > thread->fd_number) {
    thread->fd_number = fd + 1;
  }
  return thread->fds[fd];
}

fd_t* thread_set_fd(thread_t* thread, u32 fd, fd_t* nfd) {
  if (thread->fd_number > thread->fd_size) {
    log_error("thread set number limit %d\n", fd);
    return NULL;
  }
  if (fd > thread->fd_number) {
    log_error("thread set fd limit %d\n", fd);
    return NULL;
  }
  return thread->fds[fd] = nfd;
}

int thread_map(thread_t* thread, vaddr_t virt_addr, vaddr_t phy_addr, vaddr_t size) {
  log_debug("thread map %lx %lx %ld\n", virt_addr, phy_addr, size);
  vmemory_map((u64*)thread->vm->upage, virt_addr, phy_addr, size);
  return 0;
}

void thread_dump_fd(thread_t* thread) {
  for (int i = 0; i < thread->fd_number; i++) {
    fd_t* fd = thread->fds[i];
    kprintf("tid:%d fd:%d id:%d ptr:%x name:%s\n", thread->id, i, fd->id, fd,
            fd->name);
  }
}

void thread_dump(thread_t* thread, u32 flags) {
  if (thread == NULL) return;
  if (thread->dump_count >= THREAD_DUMP_STOP_COUNT) {
    log_error("thread dump count >= %d, will not dump again\n",
              THREAD_DUMP_STOP_COUNT);
    return;
  }
  thread->dump_count++;
  kprintf("id       %d\n", thread->id);
  if (thread->name != NULL) {
    kprintf("name   %s\n", thread->name);
  }
  kprintf("priority %d\n", thread->priority);
  kprintf("counter  %d\n", thread->counter);
  kprintf("state    %d\n", thread->state);
  kprintf("ksp      %08x  [%8x - %8x]\n", thread->ctx->ksp,
          thread->ctx->ksp_start, thread->ctx->ksp_end);
  kprintf("usp      %08x\n", thread->ctx->usp);
  kprintf("pid      %d\n", thread->pid);
  kprintf("fd_num   %d\n", thread->fd_number);
  kprintf("code     %d\n", thread->code);

  if ((flags & DUMP_CONTEXT) == DUMP_CONTEXT) {
    kprintf("--context--\n");
    context_dump(thread->ctx);
  }
  if ((flags & DUMP_STACK) == DUMP_STACK) {
    kprintf("--kstack--\n");
    // thread_dump_stack(thread->ctx->ksp_start, thread->ctx->ksp_size);
    int dump_size = 0x10;
    thread_dump_stack(thread->ctx->ksp_end - dump_size, dump_size);
#ifdef VM_ENABLE
    if (thread->vm != NULL && thread->vm->vma != NULL) {
      vmemory_area_t* vm = vmemory_area_find_flag(thread->vm->vma, MEMORY_STACK);
      if (vm != NULL) {
        kprintf("--ustack--\n");
        thread_dump_stack(vm->vend - dump_size, dump_size);
        kprintf("kpage    %08x\n", thread->vm->kpage);
        kprintf("upage    %08x\n", thread->vm->upage);
        kprintf("usp_rng  [%8x - %8x]\n", vm->alloc_addr,
                vm->alloc_addr + vm->alloc_size);
      }
    }
#endif
  }
  kprintf("\n");
}

void thread_dump_stack(u32* stack, u32 size) {
  int line_width = 24;
  int offset = (u32)stack;
  char* buffer = stack;
  for (int i = 0; i < size; i++) {
    if (i % line_width == 0) {
      if (i == 0) {
        kprintf(" %07x   ", offset);
      } else {
        kprintf("\n %07x   ", offset);
      }
    }
    kprintf("%02x ", 0xff & buffer[i]);
    offset++;
  }
  kprintf("\n");
}

void thread_dumps() {
  char* state_str[7] = {"create",   "running", "runnable", "stopped",
                        "waitting", "sleep",   "unkown"};
  char* str = "unkown";
  u32 idle_ticks[MAX_CPU];
  u32 cpu_total[MAX_CPU];
  int i;

  for (i = 0; i < MAX_CPU; i++) {
    idle_ticks[i] = 0;
    cpu_total[i] = schedule_get_ticks_cpu(i);
  }
  for (i = 0; i < MAX_CPU; i++) {
    for (thread_t* p = schedulable_head_thread[i]; p != NULL; p = p->next) {
      if (p->name != NULL && kstrcmp((char*)p->name, "idle") == 0) {
        int c = (int)p->cpu_id;
        if (c >= 0 && c < MAX_CPU) {
          idle_ticks[c] += p->ticks;
        }
      }
    }
  }

  kprintf("cpu");
  for (i = 0; i < MAX_CPU; i++) {
    u32 busy = 0;
    if (cpu_total[i] > 0) {
      u32 idle = idle_ticks[i];
      if (idle > cpu_total[i]) {
        idle = cpu_total[i];
      }
      busy = 100 - (idle * 100) / cpu_total[i];
    }
    kprintf(" %d:%d%%", i, busy);
  }
  kprintf("\n");

  kprintf(
      "id   pid  name                 state     cpu  cpu%%  count  "
      "  vm   pm   nstack  file  sleep  level  faults ticks\n");
  for (i = 0; i < MAX_CPU; i++) {
    for (thread_t* p = schedulable_head_thread[i]; p != NULL; p = p->next) {
      u32 pct = 0;
      u32 tot;
      int is_idle =
          (p->name != NULL && kstrcmp((char*)p->name, "idle") == 0);
      if (p->state <= THREAD_SLEEP) {
        str = state_str[p->state];
      }
      tot = (p->cpu_id < MAX_CPU) ? cpu_total[p->cpu_id] : 0;
      /*
       * cpu% = 占本核墙钟时间（含 idle）。
       * 若按 busy 占比，空闲 shell 每次醒来都会接近 100%，误显示成 init 90%。
       * stopped / idle 显示 0%（占用见顶部 cpu N:X%）。
       */
      if (p->state != THREAD_STOPPED && !is_idle && tot > 0) {
        pct = (p->ticks * 100) / tot;
        if (pct > 100) {
          pct = 100;
        }
      }
      kprintf("%-4d ", p->id);
      kprintf("%-4d ", p->pid);

      if (p->name != NULL) {
        kprintf("%-20s ", p->name);
      } else {
        kprintf("%-20s ", "");
      }
      kprintf("%-8s %4d %4d%% %6d %4dk %4dk %4dk %6d %6d %6d  %4d %6d\n", str,
              p->cpu_id, pct, p->counter,
              p->vm != NULL && p->vm->vma != NULL
                  ? p->vm->vma->alloc_size / 1024
                  : 0,
              p->mem / 1024, p->ctx->usp_size / 1024, p->fd_number,
              p->sleep_counter, p->level, p->faults, p->ticks);
    }
  }
}

void thread_run_all() {
  thread_t* v = schedulable_head_thread[cpu_get_id()];
  for (; v; v = v->next) {
    thread_run(v);
  }
}

int thread_count() {
  int count = 0;
  for (int i = 0; i < MAX_CPU; i++) {
    for (thread_t* p = schedulable_head_thread[i]; p != NULL; p = p->next) {
      count++;
    }
  }
  return count;
}

thread_t* thread_find_id(int id) {
  for (int i = 0; i < MAX_CPU; i++) {
    for (thread_t* p = schedulable_head_thread[i]; p != NULL; p = p->next) {
      if (p->id == id) {
        return p;
      }
    }
  }
  return NULL;
}

/* 【僵尸线程不在调度链上】thread_stop→thread_recycle 会 thread_remove 把退出的
 * 线程从 schedulable_head_thread 摘掉并挂到 recycle 队列，所以只查调度链永远
 * 找不到僵尸 ⇒ waitpid 直接 ECHILD(-1)（实测 system() 返回 -1）。这里连同
 * recycle 队列一起查。线程尚未被 thread_recycle_process 释放前都在该队列里。 */
thread_t* thread_find_zombie_child(int parent_tid, int pid) {
  for (int i = 0; i < MAX_CPU; i++) {
    for (thread_t* p = schedulable_head_thread[i]; p != NULL; p = p->next) {
      if ((int)p->pid != parent_tid) {
        continue;
      }
      if (p->state != THREAD_STOPPED) {
        continue;
      }
      if (pid > 0 && (int)p->id != pid) {
        continue;
      }
      return p;
    }
  }
  for (thread_t* p = recycle_head_thread; p != NULL; p = p->next) {
    if ((int)p->pid != parent_tid) {
      continue;
    }
    if (p->state != THREAD_STOPPED) {
      continue;
    }
    if (pid > 0 && (int)p->id != pid) {
      continue;
    }
    return p;
  }
  return NULL;
}

int thread_child_exists(int parent_tid, int pid) {
  for (int i = 0; i < MAX_CPU; i++) {
    for (thread_t* p = schedulable_head_thread[i]; p != NULL; p = p->next) {
      if ((int)p->pid != parent_tid) {
        continue;
      }
      if (pid > 0 && (int)p->id != pid) {
        continue;
      }
      return 1;
    }
  }
  for (thread_t* p = recycle_head_thread; p != NULL; p = p->next) {
    if ((int)p->pid != parent_tid) {
      continue;
    }
    if (pid > 0 && (int)p->id != pid) {
      continue;
    }
    return 1;
  }
  return 0;
}