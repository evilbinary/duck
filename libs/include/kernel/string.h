#ifndef _STRING_H
#define _STRING_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../types.h"
#include "stdarg.h"

void    *kmemccpy(void *, const void *, int, size_t);
void    *kmemchr(const void *, int, size_t);
int      kmemcmp(const void *, const void *, size_t);
void    *kmemcpy(void *, const void *, size_t);
void    *kmemcpy64(void *, const void *, size_t);  // 64-bit version for ARM64
void    *kmemmove(void *, const void *, size_t);
void    *kmemset(void *, int, size_t);
char    *kstrcat(char *, const char *);
char    *kstrchr(const char *, int);
int      kstrcmp(const char *, const char *);
int      kstrcoll(const char *, const char *);
char    *kstrcpy(char *, const char *);
size_t   kstrcspn(const char *, const char *);
char    *kstrdup(const char *);
char    *kstrerror(int);
size_t   kstrlen(const char *);
char    *kstrncat(char *, const char *, size_t);
int      kstrncmp(const char *, const char *, size_t);
char    *kstrncpy(char *, const char *, size_t);
char    *kstrpbrk(const char *, const char *);
char    *kstrrchr(const char *, int);
size_t   kstrspn(const char *, const char *);
char    *kstrstr(const char *, const char *);
char    *kstrtok(char *, const char *);
char    *kstrtok_r(char *, const char *, char **);
size_t   kstrxfrm(char *, const char *, size_t);

char*	kstrndup(const char *s, size_t n);	// This is not in the standard.
char*	kstrdup(const char *s);				// This is not in the standard.

int kvsprintf(char *buf, const char *fmt, va_list args);

/* 带界格式化（vsnprintf 语义）：写入绝不越过 buf[0..size-1]，结尾一定补 '\0'，
 * size==0 时不写任何字节；返回实际写入长度（不含结尾 '\0'）。
 * kvsprintf 是无界包装（保留老调用者行为），新代码请优先用它。 */
int kvsnprintf(char *buf, size_t size, const char *fmt, va_list args);

/* 带界 + 不定参的便捷包装（内部调 kvsnprintf） */
int ksnprintf(char *buf, size_t size, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif

