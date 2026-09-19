# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 01/01/20
# * 邮箱: rootdebug@163.com
# ********************************************************************
target("mod-irq")
set_kind("static")

add_deps(
    'arch',
    'kernel',
    'mod-devfs',
)

add_files(
    'irq.c'
)

add_includedirs(
    ".."
)
