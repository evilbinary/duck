/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "backtrace.h"

/* 帧指针链回溯，带边界校验：
 * - 内核态：fp 必须落在 ksp_start..ksp_end 内
 * - 用户态：fp 必须落在 MEMORY_STACK vma 内（vma 校验代替裸读，
 *           避免回溯过程自身再次触发页错误）
 * - prev 必须单调递增，否则认为帧链已损坏
 */

static vmemory_area_t* bt_find_stack_vma(thread_t* t) {
  if (t == NULL || t->vm == NULL || t->vm->vma == NULL) {
    return NULL;
  }
  return vmemory_area_find_flag(t->vm->vma, MEMORY_STACK);
}

static int bt_get_bounds(thread_t* t, int mode, u32* lo, u32* hi) {
  context_t* ctx = t != NULL ? t->ctx : NULL;
  if (mode == 3) {
    vmemory_area_t* a = bt_find_stack_vma(t);
    if (a == NULL) {
      return -1;
    }
    *lo = a->vaddr;
    *hi = a->vend;
  } else {
    if (ctx == NULL || ctx->ksp_start == NULL || ctx->ksp_end == NULL) {
      return -1;
    }
    *lo = (u32)ctx->ksp_start;
    *hi = (u32)ctx->ksp_end;
  }
  return 0;
}

static int bt_walk(thread_t* t, int mode, u32 fp, u32 pc, bt_frame_t* frames,
                   int max) {
  u32 lo, hi;
  if (bt_get_bounds(t, mode, &lo, &hi) != 0) {
    return 0;
  }
  int n = 0;
  frames[n].addr = pc;
  frames[n].mode = mode;
  n++;
  while (n < max) {
    if (fp < lo || fp > hi || (fp & 0x3) != 0) {
      break;
    }
    u32 prev, ret;
#if defined(ARMV5) || defined(ARMV7) || defined(ARMV7_A)
    /* ARM EABI (-mapcs-frame): push {fp, ip, lr, pc}, fp = old_sp - 4,
     * 栈上 [fp]=old pc [fp-4]=old lr [fp-8]=old ip [fp-12]=old fp */
    if (fp - 12 < lo) {
      break;
    }
    prev = ((u32*)fp)[-3];
    ret = ((u32*)fp)[-1];
#else
    prev = ((u32*)fp)[0];
    ret = ((u32*)fp)[1];
#endif
    if (prev <= fp || prev > hi) {
      break;
    }
    frames[n].addr = ret;
    frames[n].mode = mode;
    n++;
    fp = prev;
  }
  return n;
}

int bt_unwind(thread_t* t, interrupt_context_t* ic, bt_frame_t* frames,
              int max) {
  if (t == NULL || ic == NULL || frames == NULL || max <= 0) {
    return 0;
  }
#if defined(X86)
  int mode = GET_CPL(ic->cs);
  return bt_walk(t, mode, ic->ebp, ic->eip, frames, max);
#elif defined(ARMV5) || defined(ARMV7) || defined(ARMV7_A)
  int mode = ((ic->psr & 0x1f) == 0x10) ? 3 : 0;  // USR mode
  return bt_walk(t, mode, ic->r11, ic->pc, frames, max);
#elif defined(RISCV)
  int mode = context_get_mode(t->ctx);
  return bt_walk(t, mode, ic->s0, ic->sepc, frames, max);
#else
  return 0;
#endif
}
