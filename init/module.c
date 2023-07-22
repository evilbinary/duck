
#include "kernel/kernel.h"

// 定义模块注册宏
#define REGISTER_MODULE(module_name) {   \
  extern module_t module_name##_module; \
  module_regist(&module_name##_module );}


void modules_init(void) {
  u32 i = 0;
  u32 count = 0;

  log_info("module regist\n");

#ifdef ARM

#ifdef ARMV7
  // require
  REGISTER_MODULE(devfs);

  REGISTER_MODULE(gpio);
  REGISTER_MODULE(serial);
  REGISTER_MODULE(hello);
  REGISTER_MODULE(spi);
  REGISTER_MODULE(lcd);

#else
  // require
  REGISTER_MODULE(devfs);

  // optional module
  REGISTER_MODULE(serial);
  REGISTER_MODULE(i2c);
  REGISTER_MODULE(gpio);
  REGISTER_MODULE(spi);
  REGISTER_MODULE(gpu);
  REGISTER_MODULE(mouse);
  REGISTER_MODULE(sdhci);
  REGISTER_MODULE(fat);
  // REGISTER_MODULE(fat32);
  // REGISTER_MODULE(hello);
  REGISTER_MODULE(test);
  REGISTER_MODULE(rtc);

#endif

#elif defined(DUMMY)
  REGISTER_MODULE(hello);

#elif defined(X86)
  // require
  REGISTER_MODULE(devfs);

  // optional module
  REGISTER_MODULE(serial);
  REGISTER_MODULE(pci);
  REGISTER_MODULE(keyboard);
  REGISTER_MODULE(rtc);
  // REGISTER_MODULE(vga);
  REGISTER_MODULE(qemu);
  REGISTER_MODULE(mouse);
  REGISTER_MODULE(pty);
  REGISTER_MODULE(sb16);
  REGISTER_MODULE(ahci);
  REGISTER_MODULE(fat);
  REGISTER_MODULE(test);

#elif defined(XTENSA)
  REGISTER_MODULE(hello);

#elif defined(GENERAL)
  // require
  REGISTER_MODULE(devfs);
  REGISTER_MODULE(serial);
  REGISTER_MODULE(sdhci);
  REGISTER_MODULE(fat);

  REGISTER_MODULE(hello);

#elif defined(RISCV)
  // require
  REGISTER_MODULE(devfs);

  REGISTER_MODULE(serial);

  // REGISTER_MODULE(fat);
#else
  REGISTER_MODULE(hello);
#endif

#ifdef PTY_MODULE
  REGISTER_MODULE(pty);
#endif

#ifdef MUSL_MODULE
  extern module_t musl_module;
  REGISTER_MODULE(musl);
#endif

#ifdef EWOK_MODULE
  extern module_t ewok_module;
  REGISTER_MODULE(ewok);
#endif

#ifdef LOG_MODULE
  REGISTER_MODULE(log);
#endif

#ifdef LOADER_MODULE
  REGISTER_MODULE(loader);
#endif

  module_run_all();

  test_kernel();
}