#include "config.h"
#include "mp.h"
extern boot_info_t* boot_info;

// muti process init
void mp_init() {
#ifdef MP_ENABLE
  log_info("mp init\n");
  int cpu_nums = cpu_get_number();
  int current_cpu_id = cpu_get_id();

  // init cpu
  for (int i = 0; i < cpu_nums; i++) {
    int id = cpu_get_index(i);
    if (current_cpu_id != id) {
      cpu_init_id(id);
    }
  }

  // delay
  cpu_delay(200);

  u32 entry = 0;

  kprintf("mp init, release %d ap(s)\n", cpu_nums - 1);

  // start all cpu
  for (int i = 0; i < cpu_nums; i++) {
    int id = cpu_get_index(i);
    if (current_cpu_id != id) {
      cpu_start_id(id, entry);
    }
  }

  cpu_delay(200);

#endif
}
