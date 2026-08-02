/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#ifndef BACKTRACE_H
#define BACKTRACE_H

#include "kernel/kernel.h"

#define BT_MAX_FRAMES 64
#define BT_SYM_NAME_MAX 96

typedef struct bt_frame {
  u32 addr;
  u8 mode;  // 0=kernel 3=user
} bt_frame_t;

// 架构栈回溯：从 interrupt_context 出发，安全地沿帧指针链解出返回地址
int bt_unwind(thread_t* t, interrupt_context_t* ic, bt_frame_t* frames,
              int max);

// 符号化单个地址：内核地址查 /kernel.elf(懒加载缓存)，用户地址查应用 ELF
void bt_sym_lookup(thread_t* t, u32 addr, int mode, char* out, u32 out_size);

// 完整 dump：回溯 + 符号化 + 打印
void bt_dump(thread_t* t, interrupt_context_t* ic, u64 fault_addr);

#endif
