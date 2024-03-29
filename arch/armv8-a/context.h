/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef CONTEXT_H
#define CONTEXT_H

#include "arch/boot.h"
#include "libs/include/types.h"
#include "platform/platform.h"

typedef struct interrupt_context {
  // manual push
  u32 no;
  u32 code;

  u32 psr;
  u32 pc;  // return to user addr

  u32 r0;
  u32 r1;
  u32 r2;
  u32 r3;
  u32 r4;
  u32 r5;
  u32 r6;
  u32 r7;
  u32 r8;
  u32 r9;
  u32 r10;
  u32 r11;  // fp
  u32 r12;  // ip
  u32 sp;   // r13 user sp
  u32 lr;   // r14 user lr
} __attribute__((packed)) interrupt_context_t;

typedef struct context_t {
  interrupt_context_t* ic;
  interrupt_context_t* ksp;
  u32 usp;
  u32 eip;
  u32 level;
  u32 tid;

  u32 ksp_start;
  u32 ksp_end;

  u32 usp_size;
  u32 ksp_size;
} context_t;


#define interrupt_process(X) 

#define interrupt_entering_code(VEC, CODE) 

#define interrupt_exit_context(duck_context) 

#define interrupt_entering(VEC) interrupt_entering_code(VEC, 0)

#define interrupt_exit()        

#define interrupt_exit2()  





#define interrupt_entering(VEC) interrupt_entering_code(VEC, 0, 0)

#define context_switch_page(ctx,page_dir) cpu_set_page(page_dir)

#define context_fn(context) context->r7
#define context_ret(context) context->r0
#define context_set_entry(context, entry) (((interrupt_context_t*)context)->pc = entry);

#define context_restore(duck_context) interrupt_exit_context(duck_context->ksp);

int context_clone(context_t* des, context_t* src);
int context_init(context_t* context, u32* ksp_top, u32* usp_top, u32* entry,
                 u32 level, int cpu);
void context_dump(context_t* c);

#endif