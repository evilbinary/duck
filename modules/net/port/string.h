/* <string.h> 会先撞上 libs/include/kernel/string.h，那里没有 memset 这些名字。 */
#ifndef LWIP_PORT_STRING_H
#define LWIP_PORT_STRING_H

#include <stddef.h>

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

#endif
