/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef ARCH_INTERRUPT_H
#define ARCH_INTERRUPT_H

#include "boot.h"
#include "context.h"
#include "libs/include/types.h"

typedef void* (*interrupt_handler_t)(interrupt_context_t* context);

void timer_init(int hz);

#define INTERRUPT_SERVICE __attribute__((naked))

enum {
  EX_RESET = 1,
  EX_DATA_FAULT = 2,
  EX_SYS_CALL = 3,
  EX_TIMER = 4,
  EX_UNDEF = 5,
  EX_OTHER = 6,
  EX_PREF_ABORT = 7,
  EX_PERMISSION = 8,
  EX_KEYBOARD=9,
  EX_MOUSE=10,
};

void interrupt_regist(u32 vec, interrupt_handler_t handler);

void interrupt_init();

void interrupt_regist_service(interrupt_handler_t handler);

#endif