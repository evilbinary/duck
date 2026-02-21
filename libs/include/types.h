#ifndef TYPES_H
#define TYPES_H

#if defined(ARM64) || defined(__aarch64__)
#define _Addr long
#define _Int64 long long
#define _Reg long
#else
#define _Addr int
#define _Int64 long long
#define _Reg int
#endif


typedef char i8;
typedef unsigned char u8;
typedef short i16;
typedef unsigned short u16;
typedef int i32;
typedef unsigned int u32;

typedef long long i64;
typedef unsigned long long u64;

typedef long long illong;
typedef unsigned long long ullong;

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



// Address-sized integer type: u64 on ARM64, u32 on 32-bit platforms
#ifdef ARM64
typedef u64 vaddr_t;
#else
typedef u32 vaddr_t;
#endif

typedef unsigned int u_int;

#if !defined(_INT8_T) && !defined(int8_t)
typedef signed char int8_t;
#endif

#if !defined(_INT16_T) && !defined(int16_t)
typedef signed short int16_t;
#endif

#if !defined(_INT32_T) && !defined(int32_t)
typedef signed int int32_t;
#endif

#if !defined(_INT64_T) && !defined(int64_t)
typedef signed long long int64_t;
#endif

#if !defined(_UINT8_T) && !defined(uint8_t)
typedef unsigned char uint8_t;
#endif

#if !defined(_UINT16_T) && !defined(uint16_t)
typedef unsigned short uint16_t;
#endif

#if !defined(_UINT32_T) && !defined(uint32_t)
typedef unsigned int uint32_t;
#endif

#if !defined(_UINT64_T) && !defined(uint64_t)
typedef unsigned long long uint64_t;
#endif

#if !defined(_INTPTR_T) && !defined(intptr_t)
typedef _Addr intptr_t;
#endif

#if !defined(_UINTPTR_T) && !defined(uintptr_t)
typedef unsigned _Addr uintptr_t;
#endif

#if !defined(_SIZE_T) && !defined(size_t) && !defined(_HAVE_SIZE_T)
#define _HAVE_SIZE_T
#define _SIZE_T
typedef unsigned _Addr size_t;
#endif


#endif
