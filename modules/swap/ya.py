# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 10/18/26
# * 邮箱: rootdebug@163.com
# ********************************************************************
target("mod-swap")
set_kind("static")

add_deps(
    'kernel',
)

add_files(
    './*.c'
)

# ../modules：分区形式的 IOC_WRITE_OFFSET（mmc/sdhci.h、ahci/ahci.h）
add_includedirs('../', '../modules')
