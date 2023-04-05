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

void timer_init(int hz) {}

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
  void *mm = malloc(ptr->length);
  printf("malloc %x\n", mm);

  *(int*)mm=0x1234;

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