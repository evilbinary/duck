#ifndef TYPES_H
#define TYPES_H

#define _Addr int
#define _Int64 __INT64_TYPE__
#define _Reg int

typedef char i8;
typedef unsigned char u8;
typedef short i16;
typedef unsigned short u16;
typedef int i32;
typedef unsigned int u32;

typedef __INT64_TYPE__ i64;
typedef unsigned __INT64_TYPE__ u64;

typedef __INT64_TYPE__ illong;
typedef unsigned __INT64_TYPE__ ullong;

typedef float f32;
typedef double f64;

typedef unsigned int uint;
typedef unsigned long ulong;

#define true 1
#define false 0

#define NULL ((void *)0)

#ifdef LIBYC
#define bool char
#else
#define bool _Bool
#endif

typedef unsigned int u_int;

typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed _Int64   int64_t;

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;


#if defined(__NEED_uint64_t) && !defined(__DEFINED_uint64_t)
typedef unsigned _Int64 uint64_t;
#define __DEFINED_uint64_t
#endif

#ifndef _INTPTR_T
#define _INTPTR_T
typedef _Addr intptr_t;
#endif

#ifndef _UINTPTR_T
#define _UINTPTR_T
typedef unsigned _Addr uintptr_t;
#endif

#ifndef _SIZE_T 
#define _SIZE_T 
typedef unsigned size_t;
#endif


#endif
