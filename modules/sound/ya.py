# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 01/01/20
# * 邮箱: rootdebug@163.com
# ********************************************************************
target("mod-sb16")
set_kind("static")

add_deps(
    'kernel'
)


add_files(
    './*.c'
)
