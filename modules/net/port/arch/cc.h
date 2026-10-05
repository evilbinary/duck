#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

#include <stddef.h>
#include <stdint.h>

/* 工具链的 limits.h / unistd.h 会把 ssize_t 和一堆宿主头拉进来。 */
#define LWIP_NO_LIMITS_H 1
#define LWIP_NO_UNISTD_H 1
#define LWIP_NO_CTYPE_H  1

#ifndef INT_MAX
#define INT_MAX 2147483647
#endif

/* arch.h 看不到 SSIZE_MAX 时会把 ssize_t 定成 int。
 * 内核在 aarch64 上是 long（libs/include/kernel/type.h）。
 * 先按内核的宽度定好，并置 __DEFINED_ssize_t，type.h 就不会再定一次。 */
#if defined(__aarch64__) || defined(ARM64)
typedef long ssize_t;
#else
typedef int ssize_t;
#endif
#define __DEFINED_ssize_t
#ifndef SSIZE_MAX
#define SSIZE_MAX 2147483647
#endif

#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END
#define PACK_STRUCT_STRUCT __attribute__((packed))
#define PACK_STRUCT_FIELD(x) x

#define LWIP_PLATFORM_DIAG(x) do { kprintf x; } while (0)
#define LWIP_PLATFORM_ASSERT(x) do { kprintf("lwip assert: %s\n", (x)); } while (0)

int kprintf(const char* fmt, ...);
unsigned int lwip_port_rand(void);
#define LWIP_RAND() lwip_port_rand()

/* 即使 <string.h> 落到了内核那份，这些声明也已经可见。 */
void* memcpy(void* d, const void* s, size_t n);
void* memmove(void* d, const void* s, size_t n);
void* memset(void* d, int c, size_t n);
int memcmp(const void* a, const void* b, size_t n);
size_t strlen(const char* s);
int strcmp(const char* a, const char* b);
int strncmp(const char* a, const char* b, size_t n);
char* strcpy(char* d, const char* s);
char* strncpy(char* d, const char* s, size_t n);
char* strchr(const char* s, int c);
int atoi(const char* s);

#endif
