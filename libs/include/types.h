#ifndef TYPES_H
#define TYPES_H

#if defined(ARM64) || defined(__aarch64__)
#define _Addr long
#define _Int64 long
#define _Reg long
#else
#define _Addr int
#define _Int64 long long
#define _Reg int
#endif


typedef signed char i8;
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

// Avoid NULL conflict with musl and other C libraries
#ifndef NULL
#define NULL ((void *)0)
#endif

#ifdef LIBYC
#define bool char
#else
#define bool _Bool
#endif



// Address-sized integer type: u64 on ARM64, u32 on 32-bit platforms
#if defined(ARM64) || defined(__aarch64__)
typedef u64 vaddr_t;
#else
typedef u32 vaddr_t;
#endif

typedef unsigned int u_int;

// Standard integer types - coordinate with musl using __DEFINED_* macros
#if !defined(__DEFINED_int8_t)
typedef signed char int8_t;
#define __DEFINED_int8_t
#endif

#if !defined(__DEFINED_int16_t)
typedef signed short int16_t;
#define __DEFINED_int16_t
#endif

#if !defined(__DEFINED_int32_t)
typedef signed int int32_t;
#define __DEFINED_int32_t
#endif

#if !defined(__DEFINED_int64_t)
typedef signed _Int64 int64_t;
#define __DEFINED_int64_t
#endif

#if !defined(__DEFINED_uint8_t)
typedef unsigned char uint8_t;
#define __DEFINED_uint8_t
#endif

#if !defined(__DEFINED_uint16_t)
typedef unsigned short uint16_t;
#define __DEFINED_uint16_t
#endif

#if !defined(__DEFINED_uint32_t)
typedef unsigned int uint32_t;
#define __DEFINED_uint32_t
#endif

#if !defined(__DEFINED_uint64_t)
typedef unsigned _Int64 uint64_t;
#define __DEFINED_uint64_t
#endif

#if !defined(__DEFINED_intptr_t)
typedef _Addr intptr_t;
#define __DEFINED_intptr_t
#endif

#if !defined(__DEFINED_uintptr_t)
typedef unsigned _Addr uintptr_t;
#define __DEFINED_uintptr_t
#endif

#if !defined(__DEFINED_size_t)
typedef unsigned _Addr size_t;
#define __DEFINED_size_t
#endif


#endif
