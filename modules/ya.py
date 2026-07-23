# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 01/01/20
# * 邮箱: rootdebug@163.com
# ********************************************************************
target("modules")
set_kind("static")
add_deps(
    'arch',
    'algorithm',
    'kernel',
)

for m in get_config('modules'):
    add_deps('mod-'+m)

add_subs('./**/ya.py')