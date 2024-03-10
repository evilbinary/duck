
#include "kernel/kernel.h"

// 定义模块注册宏
#define REGISTER_MODULE(module_name)      \
  {                                       \
    extern module_t module_name##_module; \
    module_regist(&module_name##_module); \
  }

void modules_init(void) {
  u32 i = 0;
  u32 count = 0;

  log_info("module regist\n");

  // require
  REGISTER_MODULE(devfs);


#ifdef ARMV7

  REGISTER_MODULE(gpio);
  REGISTER_MODULE(serial);
  REGISTER_MODULE(hello);
  REGISTER_MODULE(spi);
  REGISTER_MODULE(lcd);

#elif ARMV5

  REGISTER_MODULE(serial);
  REGISTER_MODULE(sdhci);
  // REGISTER_MODULE(gpu);
  // REGISTER_MODULE(mouse);
  // REGISTER_MODULE(keyboard);
#ifdef FAT_MODULE
  REGISTER_MODULE(fat);
#endif

#ifdef FATFS_MODULE
  REGISTER_MODULE(fatfs);
#endif
  REGISTER_MODULE(test);


#elif ARMV7_A

  // optional module
  REGISTER_MODULE(serial);
  REGISTER_MODULE(i2c);
  REGISTER_MODULE(gpio);
  REGISTER_MODULE(spi);
  REGISTER_MODULE(gpu);
  REGISTER_MODULE(mouse);
  REGISTER_MODULE(sdhci);

#ifdef LCD_MODULE
  REGISTER_MODULE(lcd);
#endif

#ifdef FAT_MODULE
  REGISTER_MODULE(fat);
#endif

#ifdef FATFS_MODULE
  REGISTER_MODULE(fatfs);
#endif
  // REGISTER_MODULE(fat32);
  // REGISTER_MODULE(hello);
  REGISTER_MODULE(test);

  REGISTER_MODULE(rtc);

#ifdef KEYBOARD_MODULE
  REGISTER_MODULE(keyboard);
#endif

#elif defined(DUMMY)
  REGISTER_MODULE(hello);

#elif defined(X86)
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
  REGISTER_MODULE(serial);
  REGISTER_MODULE(sdhci);
  REGISTER_MODULE(fat);

  REGISTER_MODULE(hello);

#elif defined(RISCV)

  REGISTER_MODULE(serial);

  // REGISTER_MODULE(fat);
#else
  REGISTER_MODULE(hello);
#endif


#ifdef POSIX_MODULE
  REGISTER_MODULE(posix);
#endif

#ifdef PTY_MODULE
  REGISTER_MODULE(pty);
#endif

#ifdef LOG_MODULE
  REGISTER_MODULE(log);
#endif

#ifdef LOADER_MODULE
  REGISTER_MODULE(loader);
#endif

#ifdef MUSL_MODULE
  REGISTER_MODULE(musl);
#endif

#ifdef EWOK_MODULE
  REGISTER_MODULE(ewok);
#endif

#ifdef GAGA_MODULE
  REGISTER_MODULE(gaga);
#endif

#ifdef DEBUG_MODULE
  REGISTER_MODULE(ytrace);
#endif


  log_info("module regist end\n");

  module_run_all();

  log_info("module run all end\n");

  test_kernel();
}