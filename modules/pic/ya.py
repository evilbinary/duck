# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 01/01/20
# * 邮箱: rootdebug@163.com
# ********************************************************************
target("mod-pic")
set_kind("static")

add_deps(
    'kernel'
)


add_files(
    './*.c'
)

add_includedirs('.')