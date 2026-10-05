#include "lwip/opt.h"
#include "lwip/sys.h"

#include "kernel/preempt.h"
#include "kernel/schedule.h"
#include "kernel/thread.h"

extern void thread_set_arg(thread_t* thread, void* arg);

/* 收包和定时器。tcpip 线程以及阻塞中的 socket 调用都会进来。 */
void ethernetif_pump(void);

extern void cpu_delay_usec(unsigned long long count);

static int lwip_me(void) {
  thread_t* t = thread_current();
  if (t == NULL) return 1;
  return (int)t->id + 1;
}

static void spin_lock(volatile int* lock) {
  while (__sync_lock_test_and_set(lock, 1)) {
    cpu_delay_usec(1);
  }
}

static void spin_unlock(volatile int* lock) { __sync_lock_release(lock); }

/* 不能 schedule_switch：那会丢掉当前内核调用链。
 * 置 SLEEP 后继续跑一小段，时钟中断再把线程换出。aarch64 的系统调用
 * 栈在每线程的内核栈上，这样换出是安全的。单核且 SVC 里不能抢占时，
 * 下面的延时只是让出总线，收包仍由本线程的 ethernetif_pump 完成。 */
static void lwip_wait_slice(void) {
  thread_t* t = thread_current();
  ethernetif_pump();
  if (t != NULL) {
    thread_sleep(t, 10);
  }
  cpu_delay_usec(200);
}

void sys_init(void) {}

u32_t sys_now(void) { return schedule_get_ticks_cpu(0); }

unsigned int lwip_port_rand(void) {
  static unsigned int s = 1;
  s = s * 1103515245u + 12345u + sys_now();
  return s;
}

err_t sys_sem_new(sys_sem_t* sem, u8_t count) {
  if (sem == NULL) return ERR_ARG;
  sem->count = count;
  sem->lock = 0;
  sem->valid = 1;
  return ERR_OK;
}

void sys_sem_free(sys_sem_t* sem) {
  if (sem) sem->valid = 0;
}

void sys_sem_signal(sys_sem_t* sem) {
  if (sem == NULL || !sem->valid) return;
  spin_lock(&sem->lock);
  sem->count++;
  spin_unlock(&sem->lock);
}

u32_t sys_arch_sem_wait(sys_sem_t* sem, u32_t timeout) {
  u32_t start = sys_now();
  if (sem == NULL || !sem->valid) return SYS_ARCH_TIMEOUT;
  for (;;) {
    spin_lock(&sem->lock);
    if (sem->count > 0) {
      sem->count--;
      spin_unlock(&sem->lock);
      return sys_now() - start;
    }
    spin_unlock(&sem->lock);
    if (timeout != 0 && (sys_now() - start) >= timeout) return SYS_ARCH_TIMEOUT;
    lwip_wait_slice();
  }
}

err_t sys_mutex_new(sys_mutex_t* mutex) {
  if (mutex == NULL) return ERR_ARG;
  mutex->locked = 0;
  mutex->owner = 0;
  mutex->depth = 0;
  mutex->valid = 1;
  return ERR_OK;
}

void sys_mutex_free(sys_mutex_t* mutex) {
  if (mutex) mutex->valid = 0;
}

void sys_mutex_lock(sys_mutex_t* mutex) {
  int me;
  if (mutex == NULL) return;
  me = lwip_me();
  if (mutex->owner == me) {
    mutex->depth++;
    return;
  }
  spin_lock(&mutex->locked);
  mutex->owner = me;
  mutex->depth = 1;
}

void sys_mutex_unlock(sys_mutex_t* mutex) {
  if (mutex == NULL || mutex->owner != lwip_me()) return;
  if (--mutex->depth > 0) return;
  mutex->owner = 0;
  spin_unlock(&mutex->locked);
}

err_t sys_mbox_new(sys_mbox_t* mbox, int size) {
  int i;
  (void)size;
  if (mbox == NULL) return ERR_ARG;
  for (i = 0; i < LWIP_MBOX_CAP; i++) mbox->q[i] = NULL;
  mbox->head = 0;
  mbox->tail = 0;
  mbox->count = 0;
  mbox->lock = 0;
  mbox->valid = 1;
  return ERR_OK;
}

void sys_mbox_free(sys_mbox_t* mbox) {
  if (mbox) mbox->valid = 0;
}

static int mbox_push(sys_mbox_t* mbox, void* msg) {
  if (mbox->count >= LWIP_MBOX_CAP) return -1;
  mbox->q[mbox->tail] = msg;
  mbox->tail++;
  if (mbox->tail >= LWIP_MBOX_CAP) mbox->tail = 0;
  mbox->count++;
  return 0;
}

static int mbox_pop(sys_mbox_t* mbox, void** msg) {
  if (mbox->count <= 0) return -1;
  if (msg) *msg = mbox->q[mbox->head];
  mbox->head++;
  if (mbox->head >= LWIP_MBOX_CAP) mbox->head = 0;
  mbox->count--;
  return 0;
}

void sys_mbox_post(sys_mbox_t* mbox, void* msg) {
  if (mbox == NULL || !mbox->valid) return;
  for (;;) {
    spin_lock(&mbox->lock);
    if (mbox_push(mbox, msg) == 0) {
      spin_unlock(&mbox->lock);
      return;
    }
    spin_unlock(&mbox->lock);
    lwip_wait_slice();
  }
}

err_t sys_mbox_trypost(sys_mbox_t* mbox, void* msg) {
  err_t err = ERR_MEM;
  if (mbox == NULL || !mbox->valid) return ERR_ARG;
  spin_lock(&mbox->lock);
  if (mbox_push(mbox, msg) == 0) err = ERR_OK;
  spin_unlock(&mbox->lock);
  return err;
}

err_t sys_mbox_trypost_fromisr(sys_mbox_t* mbox, void* msg) {
  return sys_mbox_trypost(mbox, msg);
}

u32_t sys_arch_mbox_fetch(sys_mbox_t* mbox, void** msg, u32_t timeout) {
  u32_t start = sys_now();
  if (mbox == NULL || !mbox->valid) return SYS_ARCH_TIMEOUT;
  for (;;) {
    spin_lock(&mbox->lock);
    if (mbox_pop(mbox, msg) == 0) {
      spin_unlock(&mbox->lock);
      return sys_now() - start;
    }
    spin_unlock(&mbox->lock);
    if (timeout != 0 && (sys_now() - start) >= timeout) {
      if (msg) *msg = NULL;
      return SYS_ARCH_TIMEOUT;
    }
    lwip_wait_slice();
  }
}

u32_t sys_arch_mbox_tryfetch(sys_mbox_t* mbox, void** msg) {
  if (mbox == NULL || !mbox->valid) return SYS_MBOX_EMPTY;
  spin_lock(&mbox->lock);
  if (mbox_pop(mbox, msg) != 0) {
    spin_unlock(&mbox->lock);
    return SYS_MBOX_EMPTY;
  }
  spin_unlock(&mbox->lock);
  return 0;
}

/* aarch64 的调度只恢复 SP_EL0。第二个 EL1h 线程不会换 SP_EL1，
 * 会和正在跑的内核线程共栈，kprintf 栈上的 fmt 被踩成野指针。
 * EL1t 用自己的用户栈，和 init 同一条路。4KB 的默认用户栈不够。 */
#define LWIP_THREAD_USTACK (64 * 1024)

sys_thread_t sys_thread_new(const char* name, lwip_thread_fn thread, void* arg,
                            int stacksize, int prio) {
  thread_t* t;
  (void)stacksize;
  (void)prio;
  t = thread_create_ex((void*)thread, KERNEL_THREAD_STACK_SIZE,
                       LWIP_THREAD_USTACK, arg, LEVEL_USER, THREAD_NEW);
  if (t == NULL) {
    kprintf("lwip: thread %s create failed\n", name != NULL ? name : "?");
    return 0;
  }
  t->name = (u8*)(name != NULL ? name : "lwip");
  thread_set_arg(t, arg);
  thread_run(t);
  return (sys_thread_t)t->id;
}

sys_prot_t sys_arch_protect(void) {
  preempt_disable();
  return 0;
}

void sys_arch_unprotect(sys_prot_t pval) {
  (void)pval;
  preempt_enable();
}
