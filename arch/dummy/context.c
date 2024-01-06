/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "context.h"
#include "cpu.h"

// 获取上下文模式
int context_get_mode(context_t* context) {
  int mode = 0;

  return mode;
}

// 上下文初始化
int context_init(context_t* context, u32* ksp_top, u32* usp_top, u32* entry,
                 u32 level, int cpu) {
  if (context == NULL) {
    return;
  }
}

// 上下文切换
interrupt_context_t* context_switch(interrupt_context_t* ic, context_t* current,
                                    context_t* next) {
  if (ic == NULL || current == next) {
    return ic;
  }

  return NULL;
}

// 打印上下文信息
void context_dump(context_t* c) {}

// 克隆上下文
int context_clone(context_t* des, context_t* src)  {}

// 打印中断信息
void context_dump_interrupt(interrupt_context_t* context) {}