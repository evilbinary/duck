# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 01/01/20
# * 邮箱: rootdebug@163.com
# ********************************************************************
target("mod-fan")
set_kind("static")

add_deps(
    'arch',
    'kernel',
    'mod-devfs',
    'mod-bcm',
)

arch=get_arch()
arch_type=get_arch_type()
plat=get_plat()

# 【raspi5 专用】Pi5 风扇在 RP1 南桥（PCIe2 挂载），温度走 firmware mailbox。
# app/init/module.c 只在 RASPI5 分支 REGISTER_MODULE(fan)，其它平台是空库、
# 无符号需求，故不需要 rtc 那种 general.c 兜底。
plat_source={
    'raspi5':[
        'fan.c',
    ],
}

source=[]
if plat_source.get(plat):
    source+=plat_source.get(plat)

add_files(
    source
)
