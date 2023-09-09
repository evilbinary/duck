/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef FN_DEFINE_H
#define FN_DEFINE_H

#include "types.h"

#define DEFAULT_TYPE 1 << 0
#define KERNEL_TYPE 1 << 1
#define DEVICE_TYPE 1 << 2

#ifndef fn_malloc
    #ifdef MALLOC_TRACE
        #define fn_malloc(size) kmalloc_trace(size,__FILE__,__LINE__,__FUNCTION__)
    #else
        #define fn_malloc(size) kmalloc(size,KERNEL_TYPE)
    #endif
#endif

#ifndef fn_free
#ifdef MALLOC_TRACE
#define fn_free(size) kfree_trace(size,__FILE__,__LINE__,__FUNCTION__)
#else
#define fn_free kfree
#endif

#endif


#endif
