/* lwIP 按 libc 的名字调用字符串函数。memcpy/memset 内核里已经有了，
 * 这里再定义会和 libkernelcommon/string.c 撞符号。 */
#include "libs/include/kernel/string.h"

void* memmove(void* d, const void* s, size_t n) { return kmemmove(d, s, n); }
int memcmp(const void* a, const void* b, size_t n) { return kmemcmp(a, b, n); }
size_t strlen(const char* s) { return kstrlen(s); }
int strcmp(const char* a, const char* b) { return kstrcmp(a, b); }
int strncmp(const char* a, const char* b, size_t n) { return kstrncmp(a, b, n); }
char* strcpy(char* d, const char* s) { return kstrcpy(d, s); }
char* strncpy(char* d, const char* s, size_t n) { return kstrncpy(d, s, n); }
char* strchr(const char* s, int c) { return kstrchr(s, c); }

int atoi(const char* s) {
  int n = 0;
  int sign = 1;
  if (s == NULL) return 0;
  while (*s == ' ' || *s == '\t') s++;
  if (*s == '-') {
    sign = -1;
    s++;
  } else if (*s == '+') {
    s++;
  }
  while (*s >= '0' && *s <= '9') {
    n = n * 10 + (*s - '0');
    s++;
  }
  return sign * n;
}

int errno;
