# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 01/01/20
# * 邮箱: rootdebug@163.com
# ********************************************************************
# 内核 input 设备核心 + /dev/input/eventN：
#   input_core.c —— 能力位/absinfo/属性位登记、evdev ioctl 全集、事件队列
#                   （带单调时钟时间戳）、节点创建；
#   evdev.c      —— 相对指针设备（数据源是平台鼠标驱动）。
# 不依赖具体硬件模块，只需 kernel 的 device/vfs 设施。
target("mod-input")
set_kind("static")

add_deps(
    'kernel'
)

add_files(
    'input_core.c',
    'evdev.c',
)

add_includedirs('../')
