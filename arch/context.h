
/*******************************************************************
 * Copyright 2021-2080 evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef ARCH_CONTEXT_H
#define ARCH_CONTEXT_H


#ifdef ARM
  #ifdef ARMV7_A
    #include "armv7-a/context.h"
  #elif defined(ARMV7)
    #include "armv7/context.h"
  #elif defined(ARMV5)
    #include "armv5/context.h"
  #elif defined(ARMV8_A)
    #include "armv8-a/context.h"
  #else
    #error "no support arm"
  #endif
#elif defined(X86)
  #ifdef X86_DUCK
    #include "x86-duck/context.h"
  #else
    #include "x86/context.h"
  #endif
#elif defined(LX6)
  #include "lx6/context.h"
#elif defined(GENERAL)
#include "general/context.h"
#elif defined(RISCV)
#include "riscv/context.h"
#else
    #error "no support"
#endif


int context_init(context_t* context, u32* ksp_top, u32* usp_top, u32* entry,
                 u32 level, int cpu);
int context_clone(context_t* des, context_t* src);
interrupt_context_t* context_switch(interrupt_context_t* ic,context_t* current,context_t* next);


#endif
