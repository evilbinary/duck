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
)

plat=get_plat()

# 分层同 modules/gpu、modules/lcd：
#   cpu.c   抽象层（/dev/cpu 节点 + read 语义），所有平台都编
#   <平台>.c 后端，实现 cpu_temp_read()
#   general.c 兜底：平台没列进来时用它（永远返回失败，节点照样存在）
plat_source={
    'raspi5':[
        'raspi5.c',
    ],
}

common_source=[
    'cpu.c',
]

source=[]
if plat_source.get(plat):
    source+=plat_source.get(plat)

# raspi5 的温度来自 VideoCore firmware mailbox（mod-bcm 提供 mailbox.c）
if plat == 'raspi5':
    add_deps('mod-bcm')

# 【兜底】没有专用实现的平台用 general.c。app/init/module.c 在所有平台都
# REGISTER_MODULE(cpu)（只判 CPU_MODULE 是否在构建列表），不留符号会链接期
# 报 undefined reference cpu_module。
if not source:
    source += ['general.c']

add_files(
    source+common_source
)

add_includedirs('.')
