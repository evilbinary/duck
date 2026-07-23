/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "interrupt.h"

interrupt_handler_t interrupt_default_handler_process=NULL;

__attribute__((noinline))
void* interrupt_default_handler(interrupt_context_t* ic){
  if(interrupt_default_handler_process!=NULL){
    void* ret = interrupt_default_handler_process(ic);
    // Always return a valid ic so interrupt_exit_ret() never sets SP = NULL.
    return (ret != NULL) ? ret : ic;
  }
  return ic;
}

void interrupt_regist_service(interrupt_handler_t handler) {
  interrupt_default_handler_process = handler;
  interrupt_regist_all();
}
