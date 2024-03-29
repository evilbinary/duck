/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "lock.h"
#include "cpu.h"

void lock_init(lock_t* lock) {
  lock->ticket = 0;
  lock->turn = 0;
}

void lock_acquire(lock_t* lock) {
  if(lock==NULL){
    return;
  }
  int turn = cpu_faa(&lock->ticket);
  while (lock->turn != turn)
    ;  // do
}

void lock_release(lock_t* lock) {
  if(lock==NULL){
    return;
  } 
    //lock->turn += 1; 
    cpu_faa(&lock->turn);
}

void acquire(u32* lock) {
  while (cpu_tas(lock, 1) == 1)
    ;  // spin wait
}
void release(u32* lock) { *lock = 0; }
