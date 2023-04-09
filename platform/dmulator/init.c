#include <pthread.h>

#include "arch/arch.h"
#include "arch/io.h"

static int com_serial_init() { return 0; }

void com_write(char a) { putchar(a); }

char com_read() { return getchar(); }

void platform_init() {
  com_serial_init();
  io_add_write_channel(com_write);
}

void platform_end() { printf("platform_end\n"); }

void platform_map() {}

void *timer_fun(void *arg) {
  printf("thread fun\n");

  for (;;) {
    context_t *current = thread_current_context();
    if (current == NULL) {
      continue;
    }

    usleep(1000 * 100);

    interrupt_context_t ic;
    ic.no = EX_TIMER;
    // interrupt_entering_code(EX_TIMER, 0, 0);
    interrupt_default_handler(&ic);
    // interrupt_exit_ret();
  }
  return NULL;
}

void timer_init(int hz) {
  pthread_t thread_id;
  int result = pthread_create(&thread_id, NULL, timer_fun, NULL);
  if (result != 0) {
    printf("fail create timer error code: %d\n", result);
  }
  printf("timer init succes\n");
}

void timer_end() {}

void *core_run(void *arg) {
  kstart(arg, 0, 0);
  return 0;
}

void init_boot(boot_info_t *boot_info) {
  memory_info_t *ptr = boot_info->memory;
  boot_info->total_memory = 0;
  int count = 0;

  ptr->length = 0x1000000 * 4;  // 16M*4
  void *mm = malloc(0x1000000 * 4);
  printf("malloc %x\n", mm);

  ptr->base = mm;
  ptr->type = 1;
  boot_info->total_memory += ptr->length;
  ptr++;
  count++;
  boot_info->memory_number = count;
}

int main(int argc, char *argv[]) {
  printf("hello dumulator\n");

  boot_info_t boot;
  char *envp[26];
  envp[0] = &boot;
  init_boot(&boot);

  kstart(argc, argv, envp);
  return 0;
}