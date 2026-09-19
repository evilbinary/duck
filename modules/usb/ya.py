# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 01/01/20
# * 邮箱: rootdebug@163.com
# ********************************************************************
target("mod-usb")
set_kind("static")

add_deps(
    'kernel',
    'mod-pic'
)

arch=get_arch()
arch_type=get_arch_type()

def_arch=arch.replace("-", "_").upper()
def_arch_type=arch_type.replace( "-", "_").upper()

plat=get_plat()

# 平台特定源文件
plat_source={
    'raspi2':[
        'dwc2.c',
    ],
    'raspi3':[
        'dwc2.c',
    ],
    'v3s':[
        'sunxi.c',
    ],
    't113-s3':[
        'sunxi.c',
    ],
}

# 通用源文件
common_source=[
    'usb.c',
    'hcd.c',
    'hub.c',
    'hid.c',
    'usb_mouse.c',
]

source=[]

if plat_source.get(plat):
    source+=plat_source.get(plat)
else:
    # 【兜底】平台没有 USB host 实现时用 dummy.c：usb.c 在 common_source 里，
    # 会无条件调用 usb_host_init()/usb_device_init()，缺实现会链接失败（如 miyoo）。
    source += ['dummy.c']

source+=common_source


add_files(
    source
)

add_includedirs(
    '.',
    '../',
    '../libs/include',
    '../platform',
    '../kernel',
    '../../',
    '../platform/{plat}'
)

add_defines(def_arch)
add_defines(def_arch_type)
