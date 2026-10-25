# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 01/01/20
# * 邮箱: rootdebug@163.com
# ********************************************************************
target("mod-posix")
set_kind("static")

add_deps(
    'kernel'
)

add_includedirs(
    '../net',
    # modules/ 根也要在搜索路径里：sysfn_fs.c 的 sysfs 垫片要取显示几何，
    # 引用 ../vga/vga.h（VGA_IOC_READ_FRAMBUFFER_*），而 vga.h 内部
    # #include "pci/pci.h" 是按 modules/ 根定位的。
    '../',
)


add_files(
    './*.c'
)
