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

/* 【必须传 va_list 指针，不能按值传】AArch64 上 va_list 是 32 字节结构体：
 * 按值传参时编译器会生成 32 字节拷贝（实测为 ldp/stp q0,q1 的 128 位 SIMD 访问）。
 * 内核早期（MMU 未开）所有数据访问都是 Device-nGnRnE，该内存类型不支持 128 位访问
 * ⇒ 真机（Cortex-A76）会卡死；QEMU 不检查这些规则，所以只在真机暴露。
 * （armv7-a 的 va_list 只有一个指针，按值传 = 一个寄存器、不碰内存，故不受影响；
 *   这是 32/64 位 ABI 差异，不是 armv7 更"聪明"。）
 * 调用方一律传 &args。 */
int kvsprintf(char *buf, const char *fmt, va_list *args);

/* 带界格式化（vsnprintf 语义）：写入绝不越过 buf[0..size-1]，结尾一定补 '\0'，
 * size==0 时不写任何字节；返回实际写入长度（不含结尾 '\0'）。
 * kvsprintf 是无界包装（保留老调用者行为），新代码请优先用它。 */
int kvsnprintf(char *buf, size_t size, const char *fmt, va_list *args);

/* 带界 + 不定参的便捷包装（内部调 kvsnprintf） */
int ksnprintf(char *buf, size_t size, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif

