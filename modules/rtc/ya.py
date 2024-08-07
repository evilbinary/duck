# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 01/01/20
# * 邮箱: rootdebug@163.com
# ********************************************************************
target("mod-rtc")
set_kind("static")

add_deps(
    'arch',
    'kernel',
    'mod-devfs',
)

arch=get_arch()
arch_type=get_arch_type()
plat=get_plat()

plat_source={
    'v3s':[
        'v3s.c',
    ],
    'raspi2':[
        'bcm2836.c',
    ],
    'rk3128':[
        'rk3128.c'
    ],
    'stm32':[
        'stm32.c'
    ],
    'general':[
        'general.c'
    ],
    't113-s3':[
        't113-s3.c'
    ],
    'versatilepb':[
        'pl031.c'
    ]
}
arch_source={
    'arm': [

    ],
    'x86':[
        'rtc.c'
    ]
}
common_source=[

]


source=[]

if arch_source.get(arch_type):
    source+=arch_source.get(arch_type)

if plat_source.get(plat):
    source+=plat_source.get(plat)


add_files(
    source+common_source
)
