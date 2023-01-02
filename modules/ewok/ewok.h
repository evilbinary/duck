#ifndef EWOK_H
#define EWOK_H

#include "kernel/kernel.h"

#define int32_t i32
#define memset kmemset
#define memcpy kmemcpy
#define strcpy kstrcpy
#define strncpy kstrncpy

enum {
  EWOK_SYS_NONE = 0,
  EWOK_SYS_KPRINT,  // 1

  // proccess memory manage
  EWOK_SYS_MALLOC,   // 2
  EWOK_SYS_REALLOC,  // 3
  EWOK_SYS_FREE,     // 4

  EWOK_SYS_EXEC_ELF,      // 5
  EWOK_SYS_FORK,          // 6
  EWOK_SYS_THREAD,        // 7
  EWOK_SYS_YIELD,         // 8
  EWOK_SYS_WAIT_PID,      // 9
  EWOK_SYS_USLEEP,        // 10
  EWOK_SYS_EXIT,          // 11
  EWOK_SYS_DETACH,        // 12
  EWOK_SYS_BLOCK,         // 13
  EWOK_SYS_WAKEUP,        // 14
  EWOK_SYS_SIGNAL_SETUP,  // 15
  EWOK_SYS_SIGNAL,        // 16
  EWOK_SYS_SIGNAL_END,    // 17

  EWOK_SYS_GET_PID,        // 18
  EWOK_SYS_GET_THREAD_ID,  // 19

  EWOK_SYS_IPC_PING,   // 20
  EWOK_SYS_IPC_READY,  // 21

  EWOK_SYS_PROC_GET_CMD,  // 22
  EWOK_SYS_PROC_SET_CMD,  // 23
  EWOK_SYS_PROC_GET_UID,  // 24
  EWOK_SYS_PROC_SET_UID,  // 25

  // share memory syscalls
  EWOK_SYS_PROC_SHM_ALLOC,  // 26
  EWOK_SYS_PROC_SHM_MAP,    // 27
  EWOK_SYS_PROC_SHM_UNMAP,  // 28
  EWOK_SYS_PROC_SHM_REF,    // 29

  EWOK_SYS_GET_SYS_INFO,   // 30
  EWOK_SYS_GET_SYS_STATE,  // 31
  EWOK_SYS_GET_PROCS,           // 32

  // map mmio memory for userspace access
  EWOK_SYS_MEM_MAP,  // 33
  EWOK_SYS_V2P,      // 34
  EWOK_SYS_P2V,      // 35

  // internal proccess communication
  EWOK_SYS_IPC_SETUP,       // 36
  EWOK_SYS_IPC_CALL,        // 37
  EWOK_SYS_IPC_GET_ARG,     // 38
  EWOK_SYS_IPC_SET_RETURN,  // 39
  EWOK_SYS_IPC_GET_RETURN,  // 40
  EWOK_SYS_IPC_END,         // 41
  EWOK_SYS_IPC_DISABLE,     // 42
  EWOK_SYS_IPC_ENABLE,      // 43

  EWOK_SYS_CORE_READY,      // 44
  EWOK_SYS_CORE_PID,        // 45
  EWOK_SYS_GET_KEVENT,      // 46
  EWOK_SYS_GET_KERNEL_TIC,  // 47

  EWOK_SYS_INTR_SETUP,  // 48
  EWOK_SYS_INTR_END,    // 49

  EWOK_SYS_SAFE_SET,        // 50
  EWOK_SYS_SOFT_INT,        // 51
  EWOK_SYS_PROC_UUID,       // 52
  EWOK_SYS_CLOSE_KCONSOLE,  // 53
  EWOK_SYS_CALL_NUM         // 54
};

#define KB 1024
#define MB (1024 * KB)
#define GB (1024 * MB)

#define KERNEL_BASE 0x80000000  //=2G virtual address start base.
#define INTERRUPT_VECTOR_BASE 0xffff0000

#define MMIO_BASE (KERNEL_BASE + 1 * GB)
#define USER_STACK_TOP (KERNEL_BASE - PAGE_SIZE)

#define FB_SIZE 64 * MB
#define DMA_SIZE 256 * KB

#define ALLOCATABLE_MEMORY_START 0

#define V2P(V) ((uint32_t)(V)-KERNEL_BASE + _sys_info.phy_offset)
#define P2V(P) ((uint32_t)(P) + KERNEL_BASE - _sys_info.phy_offset)

typedef struct {
  uint32_t cpsr, pc, gpr[13], sp, lr;
} ewok_context_t;

#define PROTO_BUFFER 128

typedef struct {
  char buffer[PROTO_BUFFER];
  void* data;

  uint32_t size;
  uint32_t total_size;
  uint32_t offset;
  uint32_t pre_alloc;
} proto_t;

typedef struct {
  uint32_t phy_base;
  uint32_t v_base;
  uint32_t size;
} mmio_info_t;

typedef struct {
  uint32_t phy_base;
  uint32_t v_base;
  uint32_t size;
} fb_info_t;

typedef struct {
  uint32_t free;
  uint32_t shared;
} mem_info_t;

typedef struct {
  uint32_t size;
  uint32_t phy_base;
} dma_info_t;

/*static attr*/
typedef struct {
  char machine[32];
  char arch[16];
  uint32_t phy_mem_size;
  uint32_t phy_offset;
  uint32_t kernel_base;
  uint32_t vector_base;

  mmio_info_t mmio;
  dma_info_t dma;
  fb_info_t fb;
  uint32_t cores;
} sys_info_t;

/*dynamic attr*/
typedef struct {
  mem_info_t mem;
  uint32_t kernel_sec;
  uint32_t svc_total;
  uint32_t svc_counter[EWOK_SYS_CALL_NUM];
} sys_state_t;

enum { KEV_NONE = 0, KEV_PROC_EXIT, KEV_PROC_CREATED };

typedef struct {
  uint32_t type;
  uint32_t data[3];
} kevent_t;

// procinfo
#define PROC_INFO_CMD_MAX 256
#define PROC_MAX 128

enum { UNUSED = 0, CREATED, SLEEPING, WAIT, BLOCK, READY, RUNNING, ZOMBIE };

enum { PROC_TYPE_PROC = 0, PROC_TYPE_THREAD, PROC_TYPE_VFORK };

enum { IPC_IDLE = 0, IPC_BUSY, IPC_RETURN, IPC_DISABLED };

#define IPC_DEFAULT 0x0
#define IPC_NON_BLOCK 0x01
#define IPC_NON_RETURN 0x80000000
#define IPC_NON_RETURN_MASK 0x7fffffff

typedef struct {
  uint32_t uuid;
  int32_t type;
  uint32_t core;
  int32_t pid;
  int32_t father_pid;
  int32_t owner;
  int32_t state;
  int32_t block_by;
  int32_t wait_for;
  uint32_t start_sec;
  uint32_t run_usec;
  uint32_t heap_size;
  uint32_t shm_size;
  char cmd[PROC_INFO_CMD_MAX];
} procinfo_t;

void ewok_svc_handler(int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg4,
                      int32_t arg5, ...);

#endif