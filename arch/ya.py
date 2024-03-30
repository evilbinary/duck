# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 01/01/20
# * 邮箱: rootdebug@163.com
# ********************************************************************
target("arch")
set_kind("static")

add_deps("archcommon",'platform','kernelcommon')

add_files(
'arch.c',
'lock.c',
'pmemory.c',
'interrupt.c'
)

add_files(
        "./{arch}/*.c",
        "./{arch}/*.s")
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

