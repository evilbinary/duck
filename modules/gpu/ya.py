# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 01/01/20
# * 邮箱: rootdebug@163.com
# ********************************************************************
target("mod-gpu")
set_kind("static")

add_deps(
    'kernel',
    'mod-bcm'
)

arch=get_arch()
arch_type=get_arch_type()

def_arch=arch.replace("-", "_").upper()


def_arch_type=arch_type.replace( "-", "_").upper()

plat=get_plat()

plat_source={
    'v3s':[
        'v3s.c',
    ],
    'raspi2':[
        'bcm2836.c',
        'mailbox.c',
    ],
    'rk3128':[
        'rk3128.c'
    ],
    'miyoo':[
        'ssd202d.c'
    ],
    't113-s3':[
        't113-s3.c'
    ],
    'orangepi-pc':[
        'h3.c'
    ],
    'versatilepb':[
        'pl110.c'
    ]
}
arch_source={
    'arm':[
        'gpu.c',
    ]
}


source=[]

if arch_source.get(arch_type):
    source+=arch_source.get(arch_type)

if plat_source.get(plat):
    source+=plat_source.get(plat)


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
