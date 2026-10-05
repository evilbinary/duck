#ifndef LWIP_ARCH_SYS_ARCH_H
#define LWIP_ARCH_SYS_ARCH_H

/* 邮箱容量要盖住 lwipopts.h 里各个 MBOX_SIZE。 */
#define LWIP_MBOX_CAP 32

struct lwip_sem {
  volatile int count;
  volatile int lock;
  int valid;
};

struct lwip_mutex {
  volatile int locked;
  volatile int owner;
  int depth;
  int valid;
};

struct lwip_mbox {
  void* q[LWIP_MBOX_CAP];
  int head;
  int tail;
  int count;
  volatile int lock;
  int valid;
};

typedef struct lwip_sem sys_sem_t;
typedef struct lwip_mutex sys_mutex_t;
typedef struct lwip_mbox sys_mbox_t;
typedef int sys_thread_t;
typedef int sys_prot_t;

#define sys_sem_valid(s) (((s) != NULL) && (s)->valid)
#define sys_sem_set_invalid(s) do { if (s) (s)->valid = 0; } while (0)
#define sys_mutex_valid(m) (((m) != NULL) && (m)->valid)
#define sys_mutex_set_invalid(m) do { if (m) (m)->valid = 0; } while (0)
#define sys_mbox_valid(m) (((m) != NULL) && (m)->valid)
#define sys_mbox_set_invalid(m) do { if (m) (m)->valid = 0; } while (0)

#endif
