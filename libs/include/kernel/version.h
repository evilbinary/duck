/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary
 ********************************************************************/
/* 【uname 单一来源】内核版本与平台标识。宏由构建系统注入（ya.py）：
 *   平台:   add_defines(plat.upper())         -> RASPI3 / RASPI5 / I386_PC ...
 *   架构:   add_defines(arch.upper())         -> ARMV8_A / ARMV7_A / X86 ...
 *   架构类: add_defines(arch_type.upper())    -> ARM64 / ARM / X86 ...
 * 改版本号只动这里，uname/sysinfo 等 sys_ 实现统一取用。 */
#ifndef _KERNEL_VERSION_H
#define _KERNEL_VERSION_H

#define KERNEL_VERSION_MAJOR 1
#define KERNEL_VERSION_MINOR 0
#define KERNEL_VERSION_PATCH 0

#define _KV_STR2(x) #x
#define _KV_STR(x) _KV_STR2(x)

/* "1.0.0-yiyiya"：LTP tst_kvercmp 按 X.Y.Z 解析数字比较 */
#define KERNEL_VERSION_STRING                  \
  _KV_STR(KERNEL_VERSION_MAJOR) "." _KV_STR(KERNEL_VERSION_MINOR) \
      "." _KV_STR(KERNEL_VERSION_PATCH) "-yiyiya"

/* machine：按架构类（arch_type）推导 */
#if defined(ARM64)
#define UTS_MACHINE "aarch64"
#elif defined(X86_64)
#define UTS_MACHINE "x86_64"
#elif defined(X86)
#define UTS_MACHINE "i686"
#elif defined(ARM)
#define UTS_MACHINE "armv7l"
#elif defined(RISCV64)
#define UTS_MACHINE "riscv64"
#elif defined(RISCV)
#define UTS_MACHINE "riscv32"
#else
#define UTS_MACHINE "unknown"
#endif

/* nodename：按板卡平台（plat）推导 */
#if defined(RASPI5)
#define UTS_NODENAME "raspi5"
#elif defined(RASPI4)
#define UTS_NODENAME "raspi4"
#elif defined(RASPI3)
#define UTS_NODENAME "raspi3"
#elif defined(RASPI2)
#define UTS_NODENAME "raspi2"
#elif defined(I386_PC)
#define UTS_NODENAME "i386-pc"
#elif defined(QEMU)
#define UTS_NODENAME "qemu"
#else
#define UTS_NODENAME "yiyiya"
#endif

#define UTS_VERSION "#1 SMP " KERNEL_VERSION_STRING

#endif /* _KERNEL_VERSION_H */
