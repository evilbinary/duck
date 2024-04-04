# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 01/01/20
# * 邮箱: rootdebug@163.com
# ********************************************************************


target("kernelcommon")
set_kind("static")

add_files(
'string.c',
'vsprintf.c',
'common.c',
'io.c',
'stack_guard.c',
'math.c'
)

add_includedirs(
'../include/algorithm',
'../include/kernel',
'../include',
'../../'
)
