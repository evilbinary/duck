/* mem.c 会 include <stdio.h>。不能落到工具链那份：它再去 include <stdarg.h>
 * 时会命中内核的 libs/include/kernel/stdarg.h，__gnuc_va_list 不存在。
 * 溢出检查默认关闭，这里只留声明，避免把 newlib 拉进来。 */
#ifndef LWIP_PORT_STDIO_H
#define LWIP_PORT_STDIO_H

#include <stddef.h>

int snprintf(char* str, size_t size, const char* fmt, ...);

#endif
