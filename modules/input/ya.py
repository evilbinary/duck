# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 01/01/20
# * 邮箱: rootdebug@163.com
# ********************************************************************
# 最小 evdev 设备：/dev/input/event0（相对鼠标）。
# 数据源是平台鼠标驱动（device_find(DEVICE_MOUSE)），所以不直接依赖任何
# 具体硬件模块，只需 kernel 的 device/vfs 设施。
target("mod-input")
set_kind("static")

add_deps(
    'kernel'
)

add_files(
    'evdev.c'
)

add_includedirs('../')
