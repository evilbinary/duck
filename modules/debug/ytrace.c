/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "trace.h"

static void* syscall_hook_table[SYSCALL_NUMBER] = {0};
void** syscall_origin_table = NULL;
ytrace_t* ytrace = NULL;

voperator_t trace_operator = {.ioctl = device_ioctl,
                              .close = device_close,
                              .open = device_open,
                              .read = device_read,
                              .write = device_write,
                              .find = vfs_find,
                              .mount = vfs_mount,
                              .readdir = vfs_readdir};

static void print_trace(ytrace_t* t) {
  for (int i = 0; i < SYSCALL_NUMBER; i++) {
    if (t->counts[i] > 0) {
      kprintf("sys no %d count %d times %d avg\n", t->counts[i], t->times[i],
              t->times[i] / t->counts[i]);
    }
  }
}

static size_t read(device_t* dev, void* buf, size_t len) {
  int ret = -1;

  log_debug("print read\n");
  print_trace(dev->data);

  return ret;
}

static size_t write(device_t* dev, void* buf, size_t len) {
  u32 ret = len;

  return ret;
}

u32 ytrace_hook_call(int no, interrupt_context_t* ic) {
  // call origin
  thread_t* current = thread_current();
  int id = ytrace->cmd[0];
  int count = ytrace->cmd[1];
  int print = ytrace->cmd[2];

  if (current->id == id) {
    u32 ticks = cpu_cyclecount();
    ytrace->origin_call(no, ic);
    u32 ticks_end = cpu_cyclecount();

    u32 diff = ticks_end - ticks;
    if (diff > 0) {
      ytrace->times[no] += diff;
    }
    ytrace->counts[no]++;

    if (print == 1) {
#ifdef ARMV7_A
      kprintf("sys %d args: %x %x %x %x %x\n", no, ic->r0, ic->r1, ic->r2,
              ic->r3, ic->r4);
#else
      kprintf("sys %d \n", no);
#endif
    }

    if (ytrace->total_count > count) {
      log_debug("count %d limit end", count);
      ytrace_hook_end(ytrace);
    }

  } else if (ytrace->origin_call != NULL) {
    ytrace->origin_call(no, ic);
  } else {
    log_debug("error ytrace origin call is null\n");
  }

  return 0;
}

void ytrace_hook_init(ytrace_t* t, int* buf) {
  // buf ==> 0 tid 1 count

  log_debug("start ytrace for tid %d count %d\n", buf[0], buf[1]);
  // hook syscall
  t->origin_call = sys_fn_get_handler();
  kmemcpy(t->cmd, buf, 64);

  sys_fn_regist_handler(&ytrace_hook_call);

  cpu_pmu_enable(1);
}

void ytrace_hook_end(ytrace_t* t) {
  sys_fn_regist_handler(t->origin_call);
  cpu_pmu_enable(0);

  print_trace(t);
  log_debug("end ytrace\n");
}

static size_t ioctl(device_t* dev, u32 cmd, void* args) {
  u32 ret = 0;
  char* buf = args;
  ytrace_t* trace = dev->data;

  if (cmd == 1) {
    ytrace_hook_init(trace, args);
  } else if (cmd == 2) {
    ytrace_hook_end(trace);
  }

  return ret;
}

int ytrace_init(void) {
  kprintf("ytrace init\n");

  device_t* dev = kmalloc(sizeof(device_t), DEFAULT_TYPE);
  dev->name = "ytrace";
  dev->read = read;
  dev->write = write;
  dev->ioctl = ioctl;
  dev->data = kmalloc(sizeof(ytrace_t), DEVICE_TYPE);
  ytrace = dev->data;

  dev->id = DEVICE_TRACE;
  dev->type = DEVICE_TYPE_CHAR;
  device_add(dev);

  vnode_t* trace = vfs_create_node("trace", V_BLOCKDEVICE);
  trace->device = dev;
  trace->op = &trace_operator;

  vfs_mount(NULL, "/dev", trace);

  return 0;
}

void ytrace_exit(void) { kprintf("ytrace exit\n"); }

module_t ytrace_module = {
    .name = "ytrace", .init = ytrace_init, .exit = ytrace_exit};
