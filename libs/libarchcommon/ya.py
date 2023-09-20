# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 01/01/20
# * 邮箱: rootdebug@163.com
# ********************************************************************

target("archcommon")

set_kind("static")

if get_arch_type():
    add_files(
                "./"+get_arch_type()+"/*.c"
                )

add_includedirs(
    '../include',
    '../include/archcommon'
)

