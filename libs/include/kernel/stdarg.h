#ifndef _STDARG_H
#define _STDARG_H


typedef __builtin_va_list va_list;

#define va_start(v,l)   __builtin_va_start(v,l)
#define va_end(v)       __builtin_va_end(v)
#define va_arg(v,l)     __builtin_va_arg(v,l)
#define va_copy(d,s)    __builtin_va_copy(d,s)


typedef unsigned char * ka_list;

#define ka_start(va, last_arg) (va = (ka_list)&last_arg + sizeof(last_arg))
#define ka_arg(va, type) ( ((va += sizeof(type)) - sizeof(type)))
#define ka_end(va) (va = (ka_list)0)



// typedef char *va_list;

// /* Amount of space required in an argument list for an arg of type TYPE.
//    TYPE may alternatively be an expression whose type is used.  */

// #define __va_rounded_size(TYPE)  \
//   (((sizeof (TYPE) + sizeof (int) - 1) / sizeof (int)) * sizeof (int))

// #ifndef __sparc__
// #define va_start(AP, LASTARG) 						\
//  (AP = ((char *) &(LASTARG) + __va_rounded_size (LASTARG)))
// #else
// #define va_start(AP, LASTARG) 						\
//  (__builtin_saveregs (),						\
//   AP = ((char *) &(LASTARG) + __va_rounded_size (LASTARG)))
// #endif

// void va_end (va_list);		/* Defined in gnulib */
// #define va_end(AP)

// #define va_arg(AP, TYPE)						\
//  (AP += __va_rounded_size (TYPE),					\
//   *((TYPE *) (AP - __va_rounded_size (TYPE))))

#endif /* _STDARG_H */
