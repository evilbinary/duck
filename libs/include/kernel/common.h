/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef COMMON_H
#define COMMON_H

#include "io.h"
#include "libs/include/types.h"

void* kmemcpy(void* /* restrict */ s1, const void* /* restrict */ s2, size_t n);
void* kmemset(void* s, int c, size_t n);

#define kassert(__e) \
  ((__e) ? (void)0 : __assert_func(__FILE__, __LINE__, __FUNCTION__, #__e))

#define min(x, y)                  \
  ({                               \
    typeof(x) _min1 = (x);         \
    typeof(y) _min2 = (y);         \
    (void)(&_min1 == &_min2);      \
    _min1 < _min2 ? _min1 : _min2; \
  })

#define max(x, y)                  \
  ({                               \
    typeof(x) _max1 = (x);         \
    typeof(y) _max2 = (y);         \
    (void)(&_max1 == &_max2);      \
    _max1 > _max2 ? _max1 : _max2; \
  })

#endif