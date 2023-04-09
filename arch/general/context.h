/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef X86_DUCK_CONTEXT_H
#define X86_DUCK_CONTEXT_H


#include "arch/boot.h"
#include "libs/include/types.h"
#include "platform/platform.h"



#define GDT_SIZE 8

#define GDT_ENTRY_NULL 0
#define GDT_ENTRY_32BIT_CS 1
#define GDT_ENTRY_32BIT_DS 2
#define GDT_ENTRY_32BIT_FS 3



typedef struct interrupt_context {
  // ds
  // u32 gs, fs, es, ds;

  // all reg
  u32 edi, esi, ebp, esp_null, ebx, edx, ecx, eax;  // pushal

  u32 no;
  // interrup stack
  u32 code;
  u32 eip;
} __attribute__((packed)) interrupt_context_t;


typedef struct context_t {
  interrupt_context_t* ic;
  interrupt_context_t* ksp;
  u32 ss0, ds0;
  u32 usp, ss, ds;
  u32 eip;
  u32 level;
  u32 cpu;
  u32 tid;

  void* thread;
  void* attr;
  int flag;

  void* ksp_start;
  void* ksp_end;

  u32 usp_size;
  u32 ksp_size;
} context_t;

#if defined(__WIN32__)

#define interrupt_process(X) \
  asm volatile(              \
      "push %esp\n"          \
      "call  _" #X           \
      " \n"                  \
      "add $4,%esp\n")
#else

#define interrupt_process(X) \
  asm volatile(              \
      "push %esp\n"          \
      "call  _" #X            \
      " \n"                  \
      "add $4,%esp\n")

#endif

#define interrupt_entering_code(VEC, CODE, TYPE) \
  asm volatile(                                  \
      "push %0 \n"                               \
      "push %1 \n"                               \
      "pushal\n"                                 \
      "mov %2,%%eax\n"                           \
      :                                          \
      : "i"(CODE), "i"(VEC), "i"(GDT_ENTRY_32BIT_DS * GDT_SIZE))

#define interrupt_entering(VEC) \
  asm volatile(                 \
      "push %0 \n"              \
      "pushal\n"                \
      "mov %1,%%eax\n"          \
      :                         \
      : "i"(VEC), "i"(GDT_ENTRY_32BIT_DS * GDT_SIZE))

#define interrupt_exit() \
  asm volatile(          \
      "popal\n"          \
      "add $8,%%esp\n"   \
      "ret\n"           \
      :                  \
      :)

#define interrupt_exit_ret() \
  asm volatile(              \
      "mov %%eax,%%esp\n"    \
      "popal\n"              \
      "add $8,%%esp\n"       \
      "ret\n"               \
      :                      \
      :)

#define interrupt_exit_context(context) \
  asm volatile(                         \
      "mov %0,%%esp\n"                  \
      "popal\n"                         \
      "add $8,%%esp\n"                  \
      "ret\n"                          \
      :                                 \
      : "m"(context->ksp))

#define context_fn(context) context->eax
#define context_ret(context) context->eax
#define context_set_entry(context, entry) \
  ((interrupt_context_t*)context)->eip = entry

// #define context_restore(duck_context) interrupt_exit_context(duck_context)

#define context_switch_page(ctx,page_dir)  cpu_set_page(page_dir)

int context_init(context_t* context, u32* ksp_top, u32* usp_top, u32* entry,
                 u32 level, int cpu);
                 
int context_clone(context_t* des, context_t* src);
interrupt_context_t* context_switch(interrupt_context_t* ic,context_t* current,context_t* next);

void context_dump(context_t* c);



#endif