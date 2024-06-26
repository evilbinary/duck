# coding:utf-8
# *******************************************************************
# * Copyright 2021-present evilbinary
# * 作者: evilbinary on 01/01/20
# * 邮箱: rootdebug@163.com
# ********************************************************************
target("mod-dma")
set_kind("static")

add_deps(
    'kernel'
)

arch=get_arch()
arch_type=get_arch_type()
plat=get_plat()

plat_source={
    'v3s':[
        'sunxi-dma.c'
    ],
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
        'sunxi-dma.c'
    ],
    'orangepi-pc':[
        'dummy.c',
    ],
    'versatilepb':[
        'dummy.c'
    ],
    'f1c200s':[
        'dummy.c'
    ]
}
arch_source={
    'arm': [

    ],
    'x86':[
        'dma.c'
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

add_includedirs('../')
