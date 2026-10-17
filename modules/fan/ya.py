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
)

plat=get_plat()

# 分层同 modules/gpu、modules/lcd、modules/cpu：
#   fan.c    抽象层（/dev/fan 节点 + 档位状态），所有平台都编
#   <平台>.c 后端，实现 fan_hw_init()/fan_hw_set_level()
#   general.c 兜底：平台没列进来时用它（没有 PWM 硬件，写档位为空操作）
plat_source={
    'raspi5':[
        'raspi5.c',
    ],
}

common_source=[
    'fan.c',
]

source=[]
if plat_source.get(plat):
    source+=plat_source.get(plat)

# raspi5 的 GPIO45 funcsel/pull 走 gpio 模块（modules/gpio/rp1.c）
if plat == 'raspi5':
    add_deps('mod-gpio')

# 【兜底】没有专用实现的平台用 general.c。app/init/module.c 在所有平台都
# REGISTER_MODULE(fan)（只判 FAN_MODULE 是否在构建列表），不留符号会链接期
# 报 undefined reference fan_module。
if not source:
    source += ['general.c']

add_files(
    source+common_source
)

add_includedirs('.')
