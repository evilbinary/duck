# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 01/01/20
# * 邮箱: rootdebug@163.com
# ********************************************************************

target("algorithm")
set_kind("static")

add_files(
    'circle_queue.c',
    'pool.c',
    'queue_pool.c',
    'buffer.c'
    )

add_includedirs(
    '../include/algorithm',
    '../include',
    '../../'
    ,
    public = True
)
