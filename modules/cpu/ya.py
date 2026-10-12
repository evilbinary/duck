# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 01/01/20
# * 邮箱: rootdebug@163.com
# ********************************************************************
target("mod-cpu")
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

# 【raspi5 专用】CPU 温度走 firmware mailbox 属性通道（GET_TEMPERATURE tag），
# 独立于 fan 模块（温度不依赖 RP1/PCIe2 链路）。注册：app/init/module.c 的
# RASPI5 分支 REGISTER_MODULE(cpu)；其它平台是空库、无符号需求，
# 故不需要 rtc 那种 general.c 兜底。
plat_source={
    'raspi5':[
        'cpu.c',
    ],
}

source=[]
if plat_source.get(plat):
    source+=plat_source.get(plat)

add_files(
    source
)
