# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 01/01/20
# * 邮箱: rootdebug@163.com
# ********************************************************************
modules =get_config('modules')

for value in modules:
    mod=string.upper(value)+'_MODULE '
    add_defines(mod)

add_subs('./**/ya.py')
add_subs("./libs/*/ya.py")

# 【内核禁用浮点/SIMD】用户线程的 q0-q31 现场由调度层在线程切换时搬
# （thread_t.fp + cpu_fp_switch，见 duck/kernel/schedule.c）。前提是"内核自己
# 绝不碰 q 寄存器"——否则内核用 SIMD 做拷贝的瞬间就把用户现场冲掉了，
# 切走时存进去的就是被破坏的值（实测内核里 pre_launch/printf 等 250 处
# 都有 SIMD 指令，pre_launch 直接 ldp q0,q1/stp q0,q1 做 32 字节拷贝）。
# -mgeneral-regs-only 只禁编译器生成 SIMD/FP 指令；手写内联汇编里的
# stp/ldp qN 仍可用（cpu_fp_switch 就是靠它们搬运现场），所以本选项安全。
# 只作用于 duck（内核/模块）作用域，用户态（cairo/weston 等）不受影响。
add_cflags('-mgeneral-regs-only')