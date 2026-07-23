# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 01/01/20
# * 邮箱: rootdebug@163.com
# ********************************************************************
target("mod-power")
set_kind("static")

add_deps(
    'kernel'
)


arch=get_arch()
arch_type=get_arch_type()
plat=get_plat()

plat_source={
 
    'raspi2':[
        'dummy.c',
    ],
    'rk3128':[
        'dummy.c'
    ],
    'stm32':[
        'dummy.c'
    ],
    'general':[
        'dummy.c'
    ],
    'miyoo':[
        'dummy.c'
    ],
    't113-s3':[
        'axp209.c'
    ],
    'v3s':[
        'axp209.c'
    ],
    'orangepi-pc':[
        'axp209.c'
    ],
    'versatilepb':[
        'dummy.c'
    ],
    'f1c200s':[
        'dummy.c'
    ]
}
arch_source={
    'arm':[ 
        'power.c'
    ],
    'x86':[
        'power.c'
    ],
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

add_includedirs('../')
