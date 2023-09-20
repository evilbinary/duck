# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 01/01/20
# * 邮箱: rootdebug@163.com
# ********************************************************************
target("kernel")
set_kind("static")

add_deps("archcommon")

add_files(
    'kernel.c',
    'thread.c',
    'schedule.c',
    'syscall.c',
    'exceptions.c',
    'memory.c',
    'module.c',
    'device.c',
    'vfs.c',
    'devfn.c',
    'fd.c',
    'loader.c',
    'mp.c',
    'trace.c',
    'logger.c',
    'vma.c',
    'page.c',
    'event.c'
)

add_includedirs(
    '../platform/{plat}',
    '../libs/include',
    '../',
    '../libs/include/archcommon'
)

arch=get_arch()
def_arch=arch.replace("-", "_").upper()

arch_type=get_arch_type()
def_arch_type=arch_type.replace( "-", "_").upper()

add_defines(def_arch)
add_defines(def_arch_type)

